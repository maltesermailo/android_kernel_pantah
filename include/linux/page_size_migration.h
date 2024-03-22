/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_SIZE_MIGRATION_H
#define _LINUX_PAGE_SIZE_MIGRATION_H

#include <linux/mm.h>

static inline unsigned long vm_flags_get_nr_pages_evicted(struct vm_area_struct *vma)
{
	unsigned long nr_pages = 0;

	if (vma->vm_flags & VM_NR_EVICTED_BIT1)
		nr_pages |= 1UL;
	if (vma->vm_flags & VM_NR_EVICTED_BIT2)
		nr_pages |= 2UL;

	return nr_pages;
}

/*
 * If the end of the VMA evicted with MADV_DONTNEED,
 * lets not bother to fault them in again.
 */
static inline unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	unsigned long nr_pages = vma_pages(vma);
	unsigned long nr_evicted = vm_flags_get_nr_pages_evicted(vma);
	loff_t start, end;

	/* Potential MADV_DONTNEED was done on a range that ends at vm_end ? */
	if (!nr_evicted)
		return nr_pages;

	/*
	 * We try to limit this only to ELF files.
	 *
	 * For an ELF built with max-page-size=16kB loaded on a 4kB page size
	 * system, the maximum amout of padding pages will be 3 per segment.
	 */
	if (nr_evicted > 3)
		return nr_pages;

	/* Drop the pages now to save reclaim work later */
	end = (nr_pages + vma->vm_pgoff) << PAGE_SHIFT;
	start = end - (nr_evicted << PAGE_SHIFT);
	trace_printk("truncate %s %ld %ld %ld 0x%lx 0x%lx\n", vma->vm_file->f_path.dentry->d_name.name,
			  nr_pages, nr_evicted, start, end);
	truncate_inode_pages_range(vma->vm_file->f_mapping, start, end-1);

	/* Only return the data pages - excluding the evicted range */
	return nr_pages - nr_evicted;
}

#endif /* _LINUX_MM_H */


