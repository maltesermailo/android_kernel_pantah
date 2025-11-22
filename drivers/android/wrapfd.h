// SPDX-License-Identifier: GPL-2.0-only
/*
 * WrapFD kernel API
 *
 * Copyright (C) 2025 Google, Inc.
 */

#ifndef _LINUX_WRAPFD_H
#define _LINUX_WRAPFD_H

enum mappable_type {
	MAPPABLE_NONE,
	MAPPABLE_FOLIO_TABLE,
	MAPPABLE_DMABUF_ATTACHMENT,
};

struct mappable_data {
	enum mappable_type type;
	union {
		/* if type == MAPPABLE_FOLIO_TABLE */
		struct sg_table *table;
		/* if type == MAPPABLE_DMABUF_ATTACHMENT */
		struct dma_buf_attachment *attachment;
	};
};

/*
 * Get buffer mappable_data. Caller also gets buffer ownership.
 * Multiple calls from the owner will return the same data.
 *
 * file: wrap file to get the folios from.
 * dev: device requesting the folios and the ownership.
 * mappable: mappable data used to map the content of the buffer.
 *
 * On success returns 0. On error returns:
 * -EBADF: wrapfd is not a valid file descriptor.
 * -EBUSY: buffer is owned by someone else.
 * -ENOENT: wrap is empty (buffer got freed or moved)
 */
int wrapfd_get(struct file *file, struct device *dev,
			   struct mappable_data *mappable);

/*
 * Release buffer ownership. Caller should own the buffer. Note that the
 * caller might still keep page mappings but in that case it has to raise
 * refcounts of the folios to keep them from being freed. If the buffer
 * is reused or freed then the next wrapfd_get() call will fail with -ENOENT
 * and mapped pages will have to be unmapped and refcounts of the folios to
 * be dropped. If wrapfd_get() succeeds then previous mappings are still
 * valid and can be reused.
 *
 * file: wrap file to release ownership for.
 * dev: device requesting the operation.
 *
 * On success returns 0. On error returns:
 * -EBADF: wrapfd is not a valid file descriptor.
 * -EBUSY: buffer is not owned by the caller.
 */
int wrapfd_put(struct file *file, struct device *dev);

#endif /* _LINUX_WRAPFD_H */
