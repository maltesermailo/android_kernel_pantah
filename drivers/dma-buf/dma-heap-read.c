#include <linux/err.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/kernel_read_file.h>

/**
 * struct dma_heap_file - wrap the file, read task for dma_heap allocate use.
 * @file:		file to read from.
 * @cred:		kthread use, user cred copy to use for the read.
 * @glimit:		The size limit for gathering. Whenever the page of the
 *			gather reaches the limit, file I/O is triggered.
 *			This is the maximum limit for the current ALLOC_AND_READ
 *			operation.
 * @offset:		The offset of the file to be read.
 * @read_size:		The size of the file to be read.
 * @alloc_size:		The size of the allocation.
 */
struct dma_heap_file {
	struct file *file;
	struct cred *cred;
	size_t glimit;
	size_t offset;
	size_t read_size;
	size_t alloc_size;
};

/**
 * struct dma_heap_file_work - represents a dma_heap file read real work.
 * @vaddr:		contigous virtual address alloc by vmap, file read need.
 *
 * @start_size:		file read start offset, same to @dma_heap_file_task->roffset.
 *
 * @need_size:		file read need size, same to @dma_heap_file_task->rsize.
 *
 * @heap_file:		file wrapper.
 *
 * @list:		child node of @dma_heap_file_control->works.
 *
 * @refp:		same @dma_heap_file_task->ref, if end of read, put ref.
 *
 * @failp:		if any work io failed, set it true, pointp @dma_heap_file_task->fail.
 */
struct dma_heap_file_work {
	void *vaddr;
	ssize_t start_size;
	ssize_t need_size;
	struct dma_heap_file *heap_file;
	struct list_head list;
	atomic_t *refp;
	bool *failp;
};

/**
 * struct dma_heap_file_task - represents a dma_heap file read process
 * @ref:		current file work counter, if zero, allocate and read
 *			done.
 *
 * @roffset:		last relative read offset, current prepared work' begin file
 *			start offset.
 *
 * @rsize:		current allocated page size use to read, if reach rbatch,
 *			trigger commit.
 *
 * @nr_gathered:	current gathered page, Take the minimum value
 *			between the @glimit and the remaining allocation amount.
 *
 * @heap_file:		current dma_heap_file
 *
 * @parray:		used for vmap, size is @dma_heap_file's batch's number
 *			pages.(this is maximum). Due to single thread file read,
 *			one page array reuse in ftask prepare is OK.
 *			Each index in parray is PAGE_SIZE.(vmap need)
 *
 * @pindex:		current allocated page filled in @parray's index.
 *
 * @fail:		any work failed when file read?
 *
 * dma_heap_file_task is the production of file read, will prepare each work
 * during allocate dma_buf pages, if match current batch, then trigger commit
 * and prepare next work. After all batch queued, user going on prepare dma_buf
 * and so on, but before return dma_buf fd, need to wait file read end and
 * check read result.
 */
struct dma_heap_file_task {
	atomic_t ref;
	size_t roffset;
	size_t rsize;
	size_t nr_gathered;
	struct dma_heap_file *heap_file;
	struct page **parray;
	unsigned int pindex;
	bool fail;
};

/**
 * struct dma_heap_file_control - global control of dma_heap file read.
 * @works:		@dma_heap_file_work's list head.
 *
 * @threadwq:		wait queue for @work_thread, if commit work, @work_thread
 *			wakeup and read this work's file contains.
 *
 * @workwq:		used for main thread wait for file read end, if allocation
 *			end before file read. @dma_heap_file_task ref effect this.
 *
 * @work_thread:	file read kthread. the dma_heap_file_task work's consumer.
 *
 * @heap_fwork_cachep:	@dma_heap_file_work's cachep, it's alloc/free frequently.
 *
 * @nr_work:		global number of how many work committed.
 */
struct dma_heap_file_control {
	struct list_head works;
	spinlock_t lock; /* only lock for @works. */
	wait_queue_head_t threadwq;
	wait_queue_head_t workwq;
	struct task_struct *work_thread;
	struct kmem_cache *heap_fwork_cachep;
	atomic_t nr_work;
};

static struct dma_heap_file_control *heap_fctl;

static struct dma_heap_file_work *
init_file_work(struct dma_heap_file_task *heap_ftask)
{
	struct dma_heap_file_work *heap_fwork;
	struct dma_heap_file *heap_file = heap_ftask->heap_file;

	if (READ_ONCE(heap_ftask->fail))
		return NULL;

	heap_fwork = kmem_cache_alloc(heap_fctl->heap_fwork_cachep, GFP_KERNEL);
	if (unlikely(!heap_fwork))
		return NULL;

	/**
	 * Map the gathered page to the vmalloc area.
	 * So we get a continuous virtual address, even if the physical address
	 * is scatter, can use this to trigger file read, if use direct I/O,
	 * all content can direct read into dma-buf pages without extra copy.
	 *
	 * Now that we get vaddr page, cached pages can return to original user, so we
	 * will not effect dma-buf export even if file read not end.
	 */
	heap_fwork->vaddr = vmap(heap_ftask->parray, heap_ftask->pindex, VM_MAP,
				 PAGE_KERNEL);
	if (unlikely(!heap_fwork->vaddr)) {
		kmem_cache_free(heap_fctl->heap_fwork_cachep, heap_fwork);
		return NULL;
	}

	heap_fwork->heap_file = heap_file;
	heap_fwork->start_size = heap_ftask->roffset + heap_file->offset;
	heap_fwork->need_size = heap_ftask->rsize;
	heap_fwork->refp = &heap_ftask->ref;
	heap_fwork->failp = &heap_ftask->fail;
	atomic_inc(&heap_ftask->ref);
	return heap_fwork;
}

static void deinit_file_work(struct dma_heap_file_work *heap_fwork)
{
	vunmap(heap_fwork->vaddr);
	atomic_dec(heap_fwork->refp);
	kmem_cache_free(heap_fctl->heap_fwork_cachep, heap_fwork);
}

static int dma_heap_read_last_page(struct dma_heap_file_task *heap_ftask)
{
	struct dma_heap_file *heap_file = heap_ftask->heap_file;
	struct file *file = heap_file->file;
	size_t size = heap_file->read_size;
	size_t start = heap_ftask->roffset;
	struct page *last = NULL;
	size_t file_size, ret;
	char *buf, *pathp;
	void *buffer;

	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (unlikely(!buf))
		return -ENOMEM;

	start = PAGE_ALIGN_DOWN(size) + heap_file->offset;

	pathp = file_path(file, buf, PATH_MAX);
	if (IS_ERR(pathp)) {
		kfree(buf);
		return PTR_ERR(pathp);
	}

	last = heap_ftask->parray[heap_ftask->pindex - 1];
	/* use page's kaddr as file read buffer. */
	buffer = kmap_local_page(last);
	ret = kernel_read_file_from_path(pathp, start, &buffer,
					 size - PAGE_ALIGN_DOWN(size),
					 &file_size, READING_POLICY);

	kunmap_local(buffer);
	kfree(buf);
	return ret;
}

/**
 * dma_heap_submit_file_read -  prepare collect enough memory, going to trigger IO
 * @heap_ftask:			info that current IO needs
 *
 * This will also check if reach to tail read.
 * For direct I/O submissions, it is necessary to pay attention to file reads
 * that are not page-aligned. For the unaligned portion of the read, buffer IO
 * needs to be triggered.
 * Returns:
 *   0 if all right, negative if something wrong
 */
static int dma_heap_submit_file_read(struct dma_heap_file_task *heap_ftask)
{
	struct dma_heap_file_work *heap_fwork = init_file_work(heap_ftask);
	struct dma_heap_file *heap_file = heap_ftask->heap_file;
	size_t size = heap_file->read_size;
	int ret;

	if (unlikely(!heap_fwork))
		return -ENOMEM;

	if (heap_ftask->roffset + heap_ftask->rsize <= size)
		goto add_work;

	/**
	 * If file size is not page aligned, direct io can't process the tail.
	 * So, if reach to tail, remain the last page use buffer read.
	 */
	heap_fwork->need_size -= PAGE_SIZE;
	ret = dma_heap_read_last_page(heap_ftask);
	if (ret) {
		deinit_file_work(heap_fwork);
		return ret;
	}

add_work:
	spin_lock(&heap_fctl->lock);
	list_add_tail(&heap_fwork->list, &heap_fctl->works);
	spin_unlock(&heap_fctl->lock);
	atomic_inc(&heap_fctl->nr_work);

	wake_up(&heap_fctl->threadwq);

	heap_ftask->roffset += heap_ftask->rsize;
	heap_ftask->rsize = 0;
	heap_ftask->pindex = 0;
	heap_ftask->nr_gathered = min_t(size_t,
					PAGE_ALIGN(size) - heap_ftask->roffset,
					heap_ftask->nr_gathered);
	return 0;
}

int dma_heap_gather_file_page(struct dma_heap_file_task *heap_ftask,
			      struct page *page)
{
	struct page **array = heap_ftask->parray;
	int num = compound_nr(page), i;
	int ret;

	/*
	 * Nothing to read, skip.
	 */
	if (heap_ftask->nr_gathered == 0)
		return 0;

	/*
	 * If it is a huge page, it may cause data out of bounds.
	 */
	for (i = 0; i < num; ++i) {
		heap_ftask->rsize += PAGE_SIZE;
		array[heap_ftask->pindex++] = &page[i];
		/* already reach to limit, trigger file read. */
		if (heap_ftask->rsize >= heap_ftask->nr_gathered) {
			ret = dma_heap_submit_file_read(heap_ftask);
			if (ret)
				return ret;
			if (heap_ftask->nr_gathered == 0)
				break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dma_heap_gather_file_page);

static int dma_heap_wait_for_file_read(struct dma_heap_file_task *heap_ftask)
{
	wait_event_freezable(heap_fctl->workwq,
			     atomic_read(&heap_ftask->ref) == 0);
	return heap_ftask->fail ? -EIO : 0;
}

int dma_heap_end_file_read(struct dma_heap_file_task *heap_ftask)
{
	int ret;

	ret = dma_heap_wait_for_file_read(heap_ftask);
	kvfree(heap_ftask->parray);
	kfree(heap_ftask);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_heap_end_file_read);

struct dma_heap_file_task *
dma_heap_declare_file_read(struct dma_heap_file *heap_file)
{
	struct dma_heap_file_task *heap_ftask =
		kzalloc(sizeof(*heap_ftask), GFP_KERNEL);

	if (unlikely(!heap_ftask))
		return NULL;

	/**
	 * glimit is the maximum size which we prepare work will meet.
	 * So, direct alloc this number's page array is OK.
	 */
	heap_ftask->parray = kvmalloc_array(heap_file->glimit >> PAGE_SHIFT,
					    sizeof(struct page *), GFP_KERNEL);
	if (unlikely(!heap_ftask->parray))
		goto put;

	heap_ftask->heap_file = heap_file;
	heap_ftask->nr_gathered = heap_file->glimit;
	return heap_ftask;

put:
	kfree(heap_ftask);
	return NULL;
}
EXPORT_SYMBOL_GPL(dma_heap_declare_file_read);

static void __work_this_io(struct dma_heap_file_work *heap_fwork)
{
	struct dma_heap_file *heap_file = heap_fwork->heap_file;
	struct file *file = heap_file->file;
	ssize_t start = heap_fwork->start_size;
	ssize_t size = heap_fwork->need_size;
	void *buffer = heap_fwork->vaddr;
	const struct cred *old_cred;
	ssize_t err;
	size_t file_size;

	/* use real task's cred to read this file. */
	old_cred = override_creds(heap_file->cred);
	err = kernel_read_file(file, start, &buffer, size, &file_size,
			       READING_POLICY);
	if (err < 0)
		WRITE_ONCE(*heap_fwork->failp, true);
	revert_creds(old_cred);
}

static int dma_heap_file_work_thread(void *data)
{
	struct dma_heap_file_control *heap_fctl =
		(struct dma_heap_file_control *)data;
	struct dma_heap_file_work *worker, *tmp;
	int nr_work;

	LIST_HEAD(pages);
	LIST_HEAD(workers);

	while (true) {
		wait_event_freezable(heap_fctl->threadwq,
				     atomic_read(&heap_fctl->nr_work) > 0);
recheck:
		spin_lock(&heap_fctl->lock);
		list_splice_init(&heap_fctl->works, &workers);
		spin_unlock(&heap_fctl->lock);

		if (unlikely(kthread_should_stop())) {
			list_for_each_entry_safe(worker, tmp, &workers, list) {
				list_del(&worker->list);
				deinit_file_work(worker);
				wake_up(&heap_fctl->workwq);
			}
			break;
		}

		nr_work = 0;
		list_for_each_entry_safe(worker, tmp, &workers, list) {
			++nr_work;
			list_del(&worker->list);
			__work_this_io(worker);

			deinit_file_work(worker);
			wake_up(&heap_fctl->workwq);
		}

		if (atomic_sub_return(nr_work, &heap_fctl->nr_work) > 0)
			goto recheck;
	}
	return 0;
}

size_t dma_heap_alloc_size(struct dma_heap_file *heap_file)
{
	return heap_file->alloc_size;
}
EXPORT_SYMBOL_GPL(dma_heap_alloc_size);

#define DEFAULT_DMA_BUF_HEAPS_GATHER_LIMIT (128 << 20)
static int dma_buf_heaps_gather_limit = DEFAULT_DMA_BUF_HEAPS_GATHER_LIMIT;
module_param_named(gather_limit, dma_buf_heaps_gather_limit, int, 0644);
MODULE_PARM_DESC(gather_limit, "Asynchronous file reading, with a maximum limit on the amount to be gathered");

struct dma_file_data {
	__u32 fd;
	__u64 offset;
	__u64 size;
};

struct dma_heap_file *init_dma_heap_file(unsigned long arg)
{
	struct file *file;
	struct dma_file_data fdata;
	struct dma_heap_file *heap_file;
	size_t fsz, size;
	int ret;

	if (copy_from_user(&fdata, (struct dma_file_data __user *)arg,
				sizeof(fdata)))
		return ERR_PTR(-EFAULT);

	if (!PAGE_ALIGNED(fdata.offset) || fdata.size == 0)
		return ERR_PTR(-EINVAL);

	file = fget(fdata.fd);
	if (unlikely(!file))
		return ERR_PTR(-EBADF);
	if (!(file->f_flags & O_DIRECT)) {
		ret = -EINVAL;
		goto err;
	}

	fsz = i_size_read(file_inode(file));
	if (fdata.offset >= fsz) {
		ret = -EINVAL;
		goto err;
	}

	/*
	 * if reach the tail, do not page-align size
	 */
	size = min_t(size_t, fsz - fdata.offset, PAGE_ALIGN(fdata.size));

	heap_file = kzalloc(sizeof(*heap_file), GFP_KERNEL);
	if (unlikely(!heap_file)) {
		ret = -ENOMEM;
		goto err;
	}

	/**
	 * Selinux block our read, but actually we are reading the stand-in
	 * for this file.
	 * So save current's cred and when going to read, override mine, and
	 * end of read, revert.
	 */
	heap_file->cred = prepare_kernel_cred(current);
	if (unlikely(!heap_file->cred)) {
		ret = -ENOMEM;
		goto err_cred;
	}

	heap_file->file = file;
	heap_file->glimit = min_t(size_t, PAGE_ALIGN(size), PAGE_ALIGN(dma_buf_heaps_gather_limit));
	heap_file->offset = fdata.offset;
	heap_file->read_size = size;
	heap_file->alloc_size = fdata.size;

	return heap_file;

err_cred:
	kfree(heap_file);
err:
	fput(file);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(init_dma_heap_file);

void deinit_dma_heap_file(struct dma_heap_file *heap_file)
{
	fput(heap_file->file);
	put_cred(heap_file->cred);
	kfree(heap_file);
}
EXPORT_SYMBOL_GPL(deinit_dma_heap_file);

int dma_heap_read_init(void)
{
	int ret;

	heap_fctl = kzalloc(sizeof(*heap_fctl), GFP_KERNEL);
	if (unlikely(!heap_fctl))
		return -ENOMEM;

	INIT_LIST_HEAD(&heap_fctl->works);
	init_waitqueue_head(&heap_fctl->threadwq);
	init_waitqueue_head(&heap_fctl->workwq);

	heap_fctl->work_thread = kthread_run(dma_heap_file_work_thread,
					     heap_fctl, "heap_fwork_t");
	if (IS_ERR(heap_fctl->work_thread)) {
		ret = -ENOMEM;
		goto fail_thread;
	}

	heap_fctl->heap_fwork_cachep = KMEM_CACHE(dma_heap_file_work, 0);
	if (unlikely(!heap_fctl->heap_fwork_cachep)) {
		ret = -ENOMEM;
		goto fail_cache;
	}

	return 0;

fail_cache:
	kthread_stop(heap_fctl->work_thread);
fail_thread:
	kfree(heap_fctl);
	return ret;
}
