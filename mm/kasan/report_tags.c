// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Copyright (c) 2020 Google, Inc.
 */

#include <linux/atomic.h>

#include "kasan.h"

extern struct kasan_stack_ring stack_ring;

static const char *get_common_bug_type(struct kasan_report_info *info)
{
	/*
	 * If access_size is a negative number, then it has reason to be
	 * defined as out-of-bounds bug type.
	 *
	 * Casting negative numbers to size_t would indeed turn up as
	 * a large size_t and its value will be larger than ULONG_MAX/2,
	 * so that this can qualify as out-of-bounds.
	 */
	if (info->access_addr + info->access_size < info->access_addr)
		return "out-of-bounds";

	return "invalid-access";
}

void kasan_print_page_history(struct page *page)
{
	u64 pos, i;
	unsigned long flags;

	write_lock_irqsave(&stack_ring.lock, flags);

	pos = atomic64_read(&stack_ring.pos);

	for (i = pos - 1; i != pos - 1 - stack_ring.size; i--) {
		struct kasan_stack_ring_entry *entry = &stack_ring.entries[i % stack_ring.size];

		/* Paired with smp_store_release() in save_stack_info(). */
		struct page *pg = (void *)smp_load_acquire(&entry->page);
		u64 op, arg;
		depot_stack_handle_t stack;

		if (pg != page)
			continue;

		op = READ_ONCE(entry->op);
		arg = READ_ONCE(entry->arg);
		stack = READ_ONCE(entry->stack);

		printk(KERN_ERR "#%lx/%lx: op = 0x%lx, arg = 0x%lx\n", (unsigned long)i, (unsigned long)pos, (unsigned long)op, (unsigned long)arg);
		stack_depot_print(stack);
	}

	write_unlock_irqrestore(&stack_ring.lock, flags);
}

void kasan_complete_mode_report_info(struct kasan_report_info *info)
{
	info->bug_type = get_common_bug_type(info);
}
