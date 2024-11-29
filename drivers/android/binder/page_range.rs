// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! This module has utilities for managing a page range where unused pages may be reclaimed by a
//! vma shrinker.

// To avoid deadlocks, locks are taken in the order:
//
//  1. mmap lock
//  2. spinlock
//  3. lru spinlock
//
// The shrinker will use trylock methods because it locks them in a different order.

use core::{
    alloc::Layout,
    ffi::{c_ulong, c_void},
    marker::PhantomPinned,
    mem::{size_of, size_of_val, MaybeUninit},
    ptr,
};

use kernel::{
    bindings,
    error::Result,
    mm::{virt, Mm, MmWithUser},
    new_mutex, new_spinlock,
    page::{Page, PAGE_SHIFT, PAGE_SIZE},
    prelude::*,
    str::CStr,
    sync::{Mutex, SpinLock},
    task::Pid,
    types::ARef,
    types::{FromBytes, Opaque},
    uaccess::UserSliceReader,
};

type SpinLockGuard<'a, T> =
    kernel::sync::lock::Guard<'a, T, kernel::sync::lock::spinlock::SpinLockBackend>;

/// Represents a shrinker that can be registered with the kernel.
///
/// Each shrinker can be used by many `ShrinkablePageRange` objects.
#[repr(C)]
pub(crate) struct Shrinker {
    inner: Opaque<*mut bindings::shrinker>,
    list_lru: Opaque<bindings::list_lru>,
}

unsafe impl Send for Shrinker {}
unsafe impl Sync for Shrinker {}

impl Shrinker {
    /// Create a new shrinker.
    ///
    /// # Safety
    ///
    /// Before using this shrinker with a `ShrinkablePageRange`, the `register` method must have
    /// been called exactly once, and it must not have returned an error.
    pub(crate) const unsafe fn new() -> Self {
        Self {
            inner: Opaque::uninit(),
            list_lru: Opaque::uninit(),
        }
    }

    /// Register this shrinker with the kernel.
    pub(crate) fn register(&'static self, name: &CStr) -> Result<()> {
        // SAFETY: These fields are not yet used, so it's okay to zero them.
        unsafe {
            self.inner.get().write(ptr::null_mut());
            self.list_lru.get().write_bytes(0, 1);
        }

        // SAFETY: The field is not yet used, so we can initialize it.
        let ret = unsafe {
            bindings::__list_lru_init(self.list_lru.get(), false, ptr::null_mut(), ptr::null_mut())
        };
        if ret != 0 {
            return Err(Error::from_errno(ret));
        }

        // SAFETY: The `name` points at a valid c string.
        let shrinker = unsafe { bindings::shrinker_alloc(0, name.as_char_ptr()) };
        if shrinker.is_null() {
            // SAFETY: We initialized it, so its okay to destroy it.
            unsafe { bindings::list_lru_destroy(self.list_lru.get()) };
            return Err(Error::from_errno(ret));
        }

        // SAFETY: We're about to register the shrinker, and these are the fields we need to
        // initialize. (All other fields are already zeroed.)
        unsafe {
            ptr::addr_of_mut!((*shrinker).count_objects).write(Some(rust_shrink_count));
            ptr::addr_of_mut!((*shrinker).scan_objects).write(Some(rust_shrink_scan));
        }

        // SAFETY: The new shrinker has been fully initialized, so we can register it.
        unsafe { bindings::shrinker_register(shrinker) };

        // SAFETY: This initializes the pointer to the shrinker so that we can use it.
        unsafe { self.inner.get().write(shrinker) };

        Ok(())
    }
}

/// A container that manages a page range in a vma.
///
/// The pages can be thought of as an array of booleans of whether the pages are usable. The
/// methods `use_range` and `stop_using_range` set all booleans in a range to true or false
/// respectively. Initially, no pages are allocated. When a page is not used, it is not freed
/// immediately. Instead, it is made available to the memory shrinker to free it if the device is
/// under memory pressure.
///
/// It's okay for `use_range` and `stop_using_range` to race with each other, although there's no
/// way to know whether an index ends up with true or false if a call to `use_range` races with
/// another call to `stop_using_range` on a given index.
///
/// It's also okay for the two methods to race with themselves, e.g. if two threads call
/// `use_range` on the same index, then that's fine and neither call will return until the page is
/// allocated and mapped.
///
/// The methods that read or write to a range require that the page is marked as in use. So it is
/// _not_ okay to call `stop_using_range` on a page that is in use by the methods that read or
/// write to the page.
#[pin_data(PinnedDrop)]
pub(crate) struct ShrinkablePageRange {
    /// Shrinker object registered with the kernel.
    shrinker: &'static Shrinker,
    /// Pid using this page range. Only used as debugging information.
    pid: Pid,
    /// The mm for the relevant process.
    #[pin]
    mm: Mutex<ARef<Mm>>,
    /// Spinlock protecting changes to pages.
    #[pin]
    lock: SpinLock<Inner>,

    /// Must not move, since page info has pointers back.
    #[pin]
    _pin: PhantomPinned,
}

struct Inner {
    /// Whether there are any pages that need to be inserted into the vma.
    need_flush: bool,
    /// Array of pages.
    ///
    /// Since this is also accessed by the shrinker, we can't use a `Box`, which asserts exclusive
    /// ownership. To deal with that, we manage it using raw pointers.
    pages: *mut PageInfo,
    /// Length of the `pages` array.
    size: usize,
    /// The address of the vma to insert the pages into.
    vma_addr: usize,
}

unsafe impl Send for Inner {}
unsafe impl Sync for Inner {}

/// An array element that describes the current state of a page.
///
/// There are three states:
///
///  * Free. The page is None. The `lru` element is not queued.
///  * Available. The page is Some. The `lru` element is queued to the shrinker's lru.
///  * Used. The page is Some. The `lru` element is not queued.
///
/// When an element is available, the shrinker is able to free the page.
///
/// Separately from the above, there's also a `need_vm_insert` flag for each page, which keeps
/// track of whether `vm_insert_page` has been called.
///
/// Locking:
///
/// * Reading is always okay when holding `lock`.
/// * Writing is always okay when holding both `lock` and `mutex`.
/// * When the page is None, writing is okay with only `lock`.
/// * When the page is Some, both `lock` and `mutex` are required to write.
/// * When the page is Some, you can read given just `mutex`.
#[repr(C)]
struct PageInfo {
    lru: bindings::list_head,
    page: PageNeedInsertPacked,
    range: *const ShrinkablePageRange,
}

impl PageInfo {
    /// # Safety
    ///
    /// The caller ensures that reading from `me.page` is ok.
    unsafe fn has_page(me: *const PageInfo) -> bool {
        // SAFETY: This pointer offset is in bounds.
        let page = unsafe { ptr::addr_of!((*me).page) };

        unsafe { (*page).is_page_some() }
    }

    /// # Safety
    ///
    /// The caller ensures that reading from `me.page` is ok.
    unsafe fn need_vm_insert(me: *const PageInfo) -> bool {
        // SAFETY: This pointer offset is in bounds.
        let page = unsafe { ptr::addr_of!((*me).page) };

        unsafe { (*page).need_vm_insert() }
    }

    /// # Safety
    ///
    /// The caller ensures that writing to `me.page` is ok, and that the page is not currently set.
    unsafe fn set_page(me: *mut PageInfo, page: PageNeedInsert) {
        // SAFETY: This pointer offset is in bounds.
        let ptr = unsafe { ptr::addr_of_mut!((*me).page) };

        // SAFETY: The pointer is valid for writing, so also valid for reading.
        if unsafe { (*ptr).is_page_some() } {
            pr_err!("set_page called when there is already a page");
            // SAFETY: We will initialize the page again below.
            unsafe { ptr::drop_in_place(ptr) };
        }

        // SAFETY: The pointer is valid for writing.
        unsafe { ptr::write(ptr, page.pack()) };
    }

    /// # Safety
    ///
    /// The caller ensures that reading from `me.page` is ok.
    unsafe fn with_page<T>(me: *const PageInfo, f: impl FnOnce(Option<&Page>) -> T) -> T {
        // SAFETY: This pointer offset is in bounds.
        let ptr = unsafe { ptr::addr_of!((*me).page) };

        // SAFETY: The pointer is valid for reading.
        unsafe { (*ptr).with_page(f) }
    }

    /// # Safety
    ///
    /// The caller ensures that writing to `me.page` is ok for the duration of 'a.
    unsafe fn take_page(me: *mut PageInfo) -> PageNeedInsert {
        // SAFETY: This pointer offset is in bounds.
        let ptr = unsafe { ptr::addr_of_mut!((*me).page) };

        // SAFETY: The pointer is valid for reading.
        core::mem::take(unsafe { &mut *ptr }).unpack()
    }

    /// Add this page to the lru list, if not already in the list.
    ///
    /// # Safety
    ///
    /// The pointer must be valid, and it must be the right shrinker.
    unsafe fn list_lru_add(me: *mut PageInfo, shrinker: &'static Shrinker) {
        // SAFETY: This pointer offset is in bounds.
        let lru_ptr = unsafe { ptr::addr_of_mut!((*me).lru) };
        // SAFETY: The lru pointer is valid, and we're not using it with any other lru list.
        unsafe { bindings::list_lru_add_obj(shrinker.list_lru.get(), lru_ptr) };
    }

    /// Remove this page from the lru list, if it is in the list.
    ///
    /// # Safety
    ///
    /// The pointer must be valid, and it must be the right shrinker.
    unsafe fn list_lru_del(me: *mut PageInfo, shrinker: &'static Shrinker) {
        // SAFETY: This pointer offset is in bounds.
        let lru_ptr = unsafe { ptr::addr_of_mut!((*me).lru) };
        // SAFETY: The lru pointer is valid, and we're not using it with any other lru list.
        unsafe { bindings::list_lru_del_obj(shrinker.list_lru.get(), lru_ptr) };
    }
}

impl ShrinkablePageRange {
    /// Create a new `ShrinkablePageRange` using the given shrinker.
    pub(crate) fn new(shrinker: &'static Shrinker) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            shrinker,
            pid: kernel::current!().pid(),
            mm <- new_mutex!(Mm::mmgrab_current().ok_or(ESRCH)?, "Binder::vm_insert_page"),
            lock <- new_spinlock!(Inner {
                need_flush: false,
                pages: ptr::null_mut(),
                size: 0,
                vma_addr: 0,
            }, "ShrinkablePageRange"),
            _pin: PhantomPinned,
        })
    }

    /// Register a vma with this page range. Returns the size of the region.
    pub(crate) fn register_with_vma(&self, vma: &virt::VmArea) -> Result<usize> {
        let num_bytes = usize::min(vma.end() - vma.start(), bindings::SZ_4M as usize);
        let num_pages = num_bytes >> PAGE_SHIFT;

        if !self.mm.lock().is_same_mm(vma) {
            pr_debug!("Failed to register with vma: invalid vma->vm_mm");
            return Err(EINVAL);
        }
        if num_pages == 0 {
            pr_debug!("Failed to register with vma: size zero");
            return Err(EINVAL);
        }

        let layout = Layout::array::<PageInfo>(num_pages).map_err(|_| ENOMEM)?;
        // SAFETY: The layout has non-zero size.
        let pages = unsafe { alloc::alloc::alloc(layout) as *mut PageInfo };
        if pages.is_null() {
            return Err(ENOMEM);
        }

        // SAFETY: This just initializes the pages array.
        unsafe {
            let self_ptr = self as *const ShrinkablePageRange;
            for i in 0..num_pages {
                let info = pages.add(i);
                ptr::addr_of_mut!((*info).range).write(self_ptr);
                ptr::addr_of_mut!((*info).page).write(PageNeedInsertPacked::default());
                let lru = ptr::addr_of_mut!((*info).lru);
                ptr::addr_of_mut!((*lru).next).write(lru);
                ptr::addr_of_mut!((*lru).prev).write(lru);
            }
        }

        let mut inner = self.lock.lock();
        if inner.size > 0 {
            pr_debug!("Failed to register with vma: already registered");
            drop(inner);
            // SAFETY: The `pages` array was allocated with the same layout.
            unsafe { alloc::alloc::dealloc(pages.cast(), layout) };
            return Err(EBUSY);
        }

        inner.pages = pages;
        inner.size = num_pages;
        inner.vma_addr = vma.start();

        Ok(num_pages)
    }

    /// Make sure that the given pages are allocated and mapped.
    ///
    /// Must not be called from an atomic context.
    pub(crate) fn use_range(&self, start: usize, end: usize) -> Result<()> {
        crate::trace::trace_update_page_range(self.pid, true, start, end);

        if start >= end {
            return Ok(());
        }
        let mut inner = self.lock.lock();
        assert!(end <= inner.size);

        for i in start..end {
            // SAFETY: This pointer offset is in bounds.
            let page_info = unsafe { inner.pages.add(i) };

            // SAFETY: The pointer is valid, and we hold `inner` so reading from the page is okay.
            if unsafe { PageInfo::has_page(page_info) } {
                crate::trace::trace_alloc_lru_start(self.pid, i);

                // Since we're going to use the page, we should remove it from the lru list so that
                // the shrinker will not free it.
                //
                // SAFETY: The pointer is valid, and this is the right shrinker.
                //
                // The shrinker can't free the page between the check and this call to
                // `list_lru_del` because we hold the lock.
                unsafe { PageInfo::list_lru_del(page_info, self.shrinker) };

                crate::trace::trace_alloc_lru_end(self.pid, i);
            } else {
                // We have to allocate a new page. Use the slow path.
                drop(inner);
                crate::trace::trace_alloc_page_start(self.pid, i);
                match self.use_page_slow(i) {
                    Ok(guard) => inner = guard,
                    Err(err) => {
                        pr_warn!("Error in use_page_slow: {:?}", err);
                        return Err(err);
                    }
                }
                crate::trace::trace_alloc_page_end(self.pid, i);
            }
        }
        Ok(())
    }

    /// Mark the given page as in use, slow path.
    ///
    /// Must not be called from an atomic context.
    ///
    /// # Safety
    ///
    /// Assumes that `i` is in bounds.
    #[cold]
    fn use_page_slow(&self, i: usize) -> Result<SpinLockGuard<'_, Inner>> {
        let new_page = PageNeedInsert {
            page: Some(Page::alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO)?),
            need_vm_insert: true,
        };

        let mut inner = self.lock.lock();

        // SAFETY: This pointer offset is in bounds.
        let page_info = unsafe { inner.pages.add(i) };

        // SAFETY: We hold `inner`, so we may read.
        if unsafe { PageInfo::has_page(page_info) } {
            // The page was already there, or someone else added the page while we didn't hold the
            // spinlock.
            //
            // SAFETY: The pointer is valid, and this is the right shrinker.
            //
            // The shrinker can't free the page between the check and this call to
            // `list_lru_del` because we hold the lock.
            unsafe { PageInfo::list_lru_del(page_info, self.shrinker) };
        } else {
            // SAFETY: We hold `inner` and the page is null, so we may write.
            unsafe { PageInfo::set_page(page_info, new_page) };
            inner.need_flush = true;
        }

        Ok(inner)
    }

    /// Ensure that all pages are inserted to the vma.
    ///
    /// On multiple concurrent calls, one caller will perform the insertions and the others will
    /// wait for it using the mutex.
    ///
    /// Technically this could be simplifed to only check the relevant range of pages, but doing it
    /// this way doesn't seem too bad.
    ///
    /// Must be called from the process that owns the vma.
    pub(crate) fn flush(&self) -> Result {
        if !self.lock.lock().need_flush {
            return Ok(());
        }

        let mutex = self.mm.lock();
        let mm = MmWithUser::use_mmput_async(mutex.mmget_not_zero().ok_or(EFAULT)?);
        let mmap_read = mm.mmap_read_lock();
        let mut inner = self.lock.lock();
        let vma = mmap_read.vma_lookup(inner.vma_addr).ok_or(EFAULT)?;

        if !inner.need_flush {
            return Ok(());
        }

        for i in 0..inner.size {
            // SAFETY: `i <= inner.size` so in-bounds.
            let page = unsafe { inner.pages.add(i) };
            // SAFETY: We hold `inner` so reading is okay.
            if unsafe { !PageInfo::need_vm_insert(page) } {
                continue;
            }

            // SAFETY: We hold `inner` and `mutex` so writing is okay.
            let mut p = unsafe { PageInfo::take_page(page) };
            assert!(p.page.is_some());
            p.need_vm_insert = false;
            // SAFETY: We hold `inner` and `mutex` so writing is okay.
            unsafe { PageInfo::set_page(page, p) };

            let addr = inner.vma_addr + (i << PAGE_SHIFT);
            drop(inner);

            // SAFETY: We just set the page to non-null, so nobody else can write until we release
            // `mutex`. Thus it is safe to read.
            unsafe { PageInfo::with_page(page, |p| vma.vm_insert_page(addr, p.ok_or(EFAULT)?))? };

            inner = self.lock.lock();
        }

        inner.need_flush = false;
        Ok(())
    }

    /// If the given page is in use, then mark it as available so that the shrinker can free it.
    ///
    /// May be called from an atomic context.
    pub(crate) fn stop_using_range(&self, start: usize, end: usize) {
        crate::trace::trace_update_page_range(self.pid, false, start, end);

        if start >= end {
            return;
        }
        let inner = self.lock.lock();
        assert!(end <= inner.size);

        for i in (start..end).rev() {
            // SAFETY: The pointer is in bounds.
            let page_info = unsafe { inner.pages.add(i) };

            // SAFETY: Okay for reading since we have the lock.
            if unsafe { PageInfo::has_page(page_info) } {
                crate::trace::trace_free_lru_start(self.pid, i);

                // SAFETY: The pointer is valid, and it's the right shrinker.
                unsafe { PageInfo::list_lru_add(page_info, self.shrinker) };

                crate::trace::trace_free_lru_end(self.pid, i);
            }
        }
    }

    /// Helper for reading or writing to a range of bytes that may overlap with several pages.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    unsafe fn iterate<T>(&self, mut offset: usize, mut size: usize, mut cb: T) -> Result
    where
        T: FnMut(&Page, usize, usize) -> Result,
    {
        if size == 0 {
            return Ok(());
        }

        // SAFETY: The caller promises that the pages touched by this call are in use. It's only
        // possible for a page to be in use if we have already been registered with a vma, and we
        // only change the `pages` and `size` fields during registration with a vma, so there is no
        // race when we read them here without taking the lock.
        let (pages, num_pages) = unsafe {
            let inner = self.lock.get_ptr();
            (
                ptr::addr_of!((*inner).pages).read(),
                ptr::addr_of!((*inner).size).read(),
            )
        };
        let num_bytes = num_pages << PAGE_SHIFT;

        // Check that the request is within the buffer.
        if offset.checked_add(size).ok_or(EFAULT)? > num_bytes {
            return Err(EFAULT);
        }

        let mut page_index = offset >> PAGE_SHIFT;
        offset &= PAGE_SIZE - 1;
        while size > 0 {
            let available = usize::min(size, PAGE_SIZE - offset);
            // SAFETY: The pointer is in bounds.
            let page_info = unsafe { pages.add(page_index) };

            let f = |page: Option<&Page>| match page {
                Some(page) => cb(page, offset, available),
                None => Err(EFAULT),
            };

            // SAFETY: The caller guarantees that this page is in the "in use" state for the
            // duration of this call to `iterate`, so nobody will change the page.
            unsafe { PageInfo::with_page(page_info, f)? };

            size -= available;
            page_index += 1;
            offset = 0;
        }
        Ok(())
    }

    /// Copy from userspace into this page range.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn copy_from_user_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: usize,
        size: usize,
    ) -> Result {
        // SAFETY: `self.iterate` has the same safety requirements as `copy_from_user_slice`.
        unsafe {
            self.iterate(offset, size, |page, offset, to_copy| {
                page.copy_from_user_slice_raw(reader, offset, to_copy)
            })
        }
    }

    /// Copy from this page range into kernel space.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn read<T: FromBytes>(&self, offset: usize) -> Result<T> {
        let mut out = MaybeUninit::<T>::uninit();
        let mut out_offset = 0;
        // SAFETY: `self.iterate` has the same safety requirements as `read`.
        unsafe {
            self.iterate(offset, size_of::<T>(), |page, offset, to_copy| {
                // SAFETY: The sum of `offset` and `to_copy` is bounded by the size of T.
                let obj_ptr = (out.as_mut_ptr() as *mut u8).add(out_offset);
                // SAFETY: The pointer points is in-bounds of the `out` variable, so it is valid.
                page.read_raw(obj_ptr, offset, to_copy)?;
                out_offset += to_copy;
                Ok(())
            })?;
        }
        // SAFETY: We just initialised the data.
        Ok(unsafe { out.assume_init() })
    }

    /// Copy from kernel space into this page range.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn write<T: ?Sized>(&self, offset: usize, obj: &T) -> Result {
        let mut obj_offset = 0;
        // SAFETY: `self.iterate` has the same safety requirements as `write`.
        unsafe {
            self.iterate(offset, size_of_val(obj), |page, offset, to_copy| {
                // SAFETY: The sum of `offset` and `to_copy` is bounded by the size of T.
                let obj_ptr = (obj as *const T as *const u8).add(obj_offset);
                // SAFETY: We have a reference to the object, so the pointer is valid.
                page.write_raw(obj_ptr, offset, to_copy)?;
                obj_offset += to_copy;
                Ok(())
            })
        }
    }

    /// Write zeroes to the given range.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn fill_zero(&self, offset: usize, size: usize) -> Result {
        // SAFETY: `self.iterate` has the same safety requirements as `copy_into`.
        unsafe {
            self.iterate(offset, size, |page, offset, len| {
                page.fill_zero_raw(offset, len)
            })
        }
    }
}

#[pinned_drop]
impl PinnedDrop for ShrinkablePageRange {
    fn drop(self: Pin<&mut Self>) {
        let (pages, size) = {
            let lock = self.lock.lock();
            (lock.pages, lock.size)
        };

        if size == 0 {
            return;
        }

        // This is the destructor, so unlike the other methods, we only need to worry about races
        // with the shrinker here.
        for i in 0..size {
            // SAFETY: The pointer is valid and it's the right shrinker.
            unsafe { PageInfo::list_lru_del(pages.add(i), self.shrinker) };
            // SAFETY: If the shrinker was going to free this page, then it would have taken it
            // from the PageInfo before releasing the lru lock. Thus, the call to `list_lru_del`
            // will either remove it before the shrinker can access it, or the shrinker will
            // already have taken the page at this point.
            unsafe { drop(PageInfo::take_page(pages.add(i))) };
        }

        // SAFETY: This computation did not overflow when allocating the pages array, so it will
        // not overflow this time.
        let layout = unsafe { Layout::array::<PageInfo>(size).unwrap_unchecked() };

        // SAFETY: The `pages` array was allocated with the same layout.
        unsafe { alloc::alloc::dealloc(pages.cast(), layout) };
    }
}

#[no_mangle]
unsafe extern "C" fn rust_shrink_count(
    shrink: *mut bindings::shrinker,
    _sc: *mut bindings::shrink_control,
) -> c_ulong {
    // SAFETY: This method is only used with the `Shrinker` type, and the cast is valid since
    // `shrinker` is the first field of a #[repr(C)] struct.
    let shrinker = unsafe { &*shrink.cast::<Shrinker>() };
    // SAFETY: Accessing the lru list is okay. Just an FFI call.
    unsafe { bindings::list_lru_count(shrinker.list_lru.get()) }
}

#[no_mangle]
unsafe extern "C" fn rust_shrink_scan(
    shrink: *mut bindings::shrinker,
    sc: *mut bindings::shrink_control,
) -> c_ulong {
    // SAFETY: This method is only used with the `Shrinker` type, and the cast is valid since
    // `shrinker` is the first field of a #[repr(C)] struct.
    let shrinker = unsafe { &*shrink.cast::<Shrinker>() };
    // SAFETY: Caller guarantees that it is safe to read this field.
    let nr_to_scan = unsafe { (*sc).nr_to_scan };
    // SAFETY: Accessing the lru list is okay. Just an FFI call.
    unsafe {
        extern "C" {
            fn rust_shrink_free_page_wrap(
                item: *mut bindings::list_head,
                list: *mut bindings::list_lru_one,
                lock: *mut bindings::spinlock_t,
                cb_arg: *mut core::ffi::c_void,
            ) -> bindings::lru_status;
        }

        bindings::list_lru_walk(
            shrinker.list_lru.get(),
            Some(rust_shrink_free_page_wrap),
            ptr::null_mut(),
            nr_to_scan,
        )
    }
}

const LRU_SKIP: bindings::lru_status = bindings::lru_status_LRU_SKIP;
const LRU_REMOVED_ENTRY: bindings::lru_status = bindings::lru_status_LRU_REMOVED_RETRY;

#[no_mangle]
unsafe extern "C" fn rust_shrink_free_page(
    item: *mut bindings::list_head,
    lru: *mut bindings::list_lru_one,
    lru_lock: *mut bindings::spinlock_t,
    _cb_arg: *mut c_void,
) -> bindings::lru_status {
    // Fields that should survive after unlocking the lru lock.
    let pid;
    let page;
    let page_index;
    let mutex;
    let mm;
    let mmap_read;
    let vma_addr;

    {
        // SAFETY: The `list_head` field is first in `PageInfo`.
        let info = item as *mut PageInfo;
        let range = unsafe { &*((*info).range) };

        mutex = match range.mm.trylock() {
            Some(guard) => guard,
            None => return LRU_SKIP,
        };

        mm = match mutex.mmget_not_zero() {
            Some(mm) => MmWithUser::use_mmput_async(mm),
            None => return LRU_SKIP,
        };

        mmap_read = match mm.mmap_read_trylock() {
            Some(guard) => guard,
            None => return LRU_SKIP,
        };

        // We can't lock it normally here, since we hold the lru lock.
        let inner = match range.lock.trylock() {
            Some(inner) => inner,
            None => return LRU_SKIP,
        };

        // SAFETY: The item is in this lru list, so it's okay to remove it.
        unsafe { bindings::list_lru_isolate(lru, item) };

        // SAFETY: Both pointers are in bounds of the same allocation.
        page_index = unsafe { info.offset_from(inner.pages) } as usize;
        pid = range.pid;

        crate::trace::trace_unmap_kernel_start(pid, page_index);

        // SAFETY: We hold the spinlock, so we can take the page.
        //
        // This sets the page pointer to zero before we unmap it from the vma. However, we call
        // `zap_page_range` before we release the mmap lock, so `use_page_slow` will not be able to
        // insert a new page until after our call to `zap_page_range`.
        page = unsafe { PageInfo::take_page(info) };
        vma_addr = inner.vma_addr;

        crate::trace::trace_unmap_kernel_end(pid, page_index);

        // From this point on, we don't access this PageInfo or ShrinkablePageRange again, because
        // they can be freed at any point after we unlock `lru_lock`.
    }

    // SAFETY: The lru lock is locked when this method is called.
    unsafe { bindings::spin_unlock(lru_lock) };

    if let Some(vma) = mmap_read.vma_lookup(vma_addr) {
        let user_page_addr = vma_addr + (page_index << PAGE_SHIFT);
        crate::trace::trace_unmap_user_start(pid, page_index);
        vma.zap_page_range_single(user_page_addr, PAGE_SIZE);
        crate::trace::trace_unmap_user_end(pid, page_index);
    }

    drop(mmap_read);
    drop(mutex);
    drop(mm);
    drop(page);

    // SAFETY: We just unlocked the lru lock, but it should be locked when we return.
    unsafe { bindings::spin_lock(lru_lock) };

    LRU_REMOVED_ENTRY
}

use page_bit::{PageNeedInsert, PageNeedInsertPacked};

/// Helper for storing data in the lower bit of a `struct page` pointer.
mod page_bit {
    use core::mem::{align_of, size_of, transmute, ManuallyDrop};
    use core::ptr::NonNull;
    use kernel::{bindings, page::Page};

    const _: () = {
        let page_ptr_size = size_of::<NonNull<bindings::page>>();
        let page_rust_size = size_of::<Page>();
        let page_opt_rust_size = size_of::<Option<Page>>();
        assert!(page_ptr_size == page_rust_size);
        assert!(page_opt_rust_size == page_rust_size);
    };

    const _: () = {
        let page_align = align_of::<bindings::page>();
        assert!(page_align != 1);
    };

    /// A version of [`PageNeedInsert`] packed into one pointer.
    pub(super) struct PageNeedInsertPacked {
        value: *mut bindings::page,
    }

    pub(super) struct PageNeedInsert {
        pub(super) page: Option<Page>,
        pub(super) need_vm_insert: bool,
    }

    const INSERTED_MASK: usize = 1;
    const PTR_MASK: usize = !INSERTED_MASK;

    impl PageNeedInsertPacked {
        pub(super) fn unpack(self) -> PageNeedInsert {
            let me = ManuallyDrop::new(self);
            let need_vm_insert = (me.value as usize) & INSERTED_MASK == INSERTED_MASK;

            let ptr = (me.value as usize & PTR_MASK) as *mut bindings::page;
            // SAFETY: `Page` has only one field of type `NonNull<page>`, and it has the same size
            // as `NonNull<page>`, so the layouts are compatible. The null-pointer optimization
            // applies, so wrapping in `Option` is also okay.
            let page = unsafe { transmute::<*mut bindings::page, Option<Page>>(ptr) };

            PageNeedInsert {
                page,
                need_vm_insert,
            }
        }

        pub(super) fn is_page_some(&self) -> bool {
            self.value as usize & PTR_MASK != 0
        }

        pub(super) fn need_vm_insert(&self) -> bool {
            self.value as usize & INSERTED_MASK == INSERTED_MASK
        }

        pub(super) fn with_page<T>(&self, f: impl FnOnce(Option<&Page>) -> T) -> T {
            let ptr = (self.value as usize & PTR_MASK) as *mut bindings::page;
            // SAFETY: Same as `unpack` except we're not taking ownership of the page.
            let page =
                ManuallyDrop::new(unsafe { transmute::<*mut bindings::page, Option<Page>>(ptr) });

            f(Option::as_ref(&page))
        }
    }

    impl PageNeedInsert {
        pub(super) fn pack(self) -> PageNeedInsertPacked {
            // SAFETY: Safe for same reason as in `pack`.
            let ptr = unsafe { transmute::<Option<Page>, *mut bindings::page>(self.page) };
            let inserted_bit = self.need_vm_insert as usize;

            PageNeedInsertPacked {
                value: ((ptr as usize) | inserted_bit) as *mut bindings::page,
            }
        }
    }

    impl Drop for PageNeedInsertPacked {
        fn drop(&mut self) {
            drop(Self::unpack(Self { value: self.value }));
        }
    }

    impl Default for PageNeedInsertPacked {
        fn default() -> Self {
            Self {
                value: core::ptr::null_mut(),
            }
        }
    }
}
