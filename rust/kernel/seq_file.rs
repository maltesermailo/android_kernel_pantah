// SPDX-License-Identifier: GPL-2.0

//! Seq file bindings.
//!
<<<<<<< HEAD   (cad8f8 ANDROID: Update rustc to 1.82.0)
//! C header: [`include/linux/seq_file.h`](../../../../include/linux/seq_file.h)

use crate::{bindings, c_str, types::Opaque};

/// A helper for implementing special files, where the complete contents can be generated on each
/// access.
pub struct SeqFile(Opaque<bindings::seq_file>);

impl SeqFile {
    /// Creates a new [`SeqFile`] from a raw pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that, for the duration of 'a, the pointer must point at a valid
    /// `seq_file` and that it will not be accessed via anything other than the returned reference.
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::seq_file) -> &'a mut SeqFile {
        // SAFETY: The safety requirements guarantee the validity of the dereference, while the
        // `Credential` type being transparent makes the cast ok.
        unsafe { &mut *ptr.cast() }
    }

    /// Used by the [`seq_print`] macro.
    ///
    /// [`seq_print`]: crate::seq_print
    pub fn call_printf(&mut self, args: core::fmt::Arguments<'_>) {
        // SAFETY: Passing a void pointer to `Arguments` is valid for `%pA`.
        unsafe {
            bindings::seq_printf(
                self.0.get(),
                c_str!("%pA").as_char_ptr(),
                &args as *const _ as *const core::ffi::c_void,
            );
        }
    }
}

/// Use for writing to a [`SeqFile`] with the ordinary Rust formatting syntax.
#[macro_export]
macro_rules! seq_print {
    ($m:expr, $($arg:tt)+) => (
        $m.call_printf(format_args!($($arg)+))
    );
}
||||||| BASE
=======
//! C header: [`include/linux/seq_file.h`](srctree/include/linux/seq_file.h)

use crate::{bindings, c_str, types::NotThreadSafe, types::Opaque};

/// A utility for generating the contents of a seq file.
#[repr(transparent)]
pub struct SeqFile {
    inner: Opaque<bindings::seq_file>,
    _not_send: NotThreadSafe,
}

impl SeqFile {
    /// Creates a new [`SeqFile`] from a raw pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that for the duration of 'a the following is satisfied:
    /// * The pointer points at a valid `struct seq_file`.
    /// * The `struct seq_file` is not accessed from any other thread.
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::seq_file) -> &'a SeqFile {
        // SAFETY: The caller ensures that the reference is valid for 'a. There's no way to trigger
        // a data race by using the `&SeqFile` since this is the only thread accessing the seq_file.
        //
        // CAST: The layout of `struct seq_file` and `SeqFile` is compatible.
        unsafe { &*ptr.cast() }
    }

    /// Used by the [`seq_print`] macro.
    pub fn call_printf(&self, args: core::fmt::Arguments<'_>) {
        // SAFETY: Passing a void pointer to `Arguments` is valid for `%pA`.
        unsafe {
            bindings::seq_printf(
                self.inner.get(),
                c_str!("%pA").as_char_ptr(),
                &args as *const _ as *const core::ffi::c_void,
            );
        }
    }
}

/// Write to a [`SeqFile`] with the ordinary Rust formatting syntax.
#[macro_export]
macro_rules! seq_print {
    ($m:expr, $($arg:tt)+) => (
        $m.call_printf(format_args!($($arg)+))
    );
}
pub use seq_print;
>>>>>>> BRANCH (544ae1 ANDROID: GKI: enable CONFIG_CMA_SYSFS)
