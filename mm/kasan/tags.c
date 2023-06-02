// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common tag-based KASAN code.
 *
 * Copyright (c) 2018 Google, Inc.
 * Copyright (c) 2020 Google, Inc.
 */

#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/static_key.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"
#include "../slab.h"

#define KASAN_STACK_RING_SIZE_DEFAULT (32 << 10)

enum kasan_arg_stacktrace {
	KASAN_ARG_STACKTRACE_DEFAULT,
	KASAN_ARG_STACKTRACE_OFF,
	KASAN_ARG_STACKTRACE_ON,
};

static enum kasan_arg_stacktrace kasan_arg_stacktrace __initdata;

/* Whether to collect alloc/free stack traces. */
DEFINE_STATIC_KEY_TRUE(kasan_flag_stacktrace);

/* Non-zero, as initial pointer values are 0. */
#define STACK_RING_BUSY_PTR ((void *)1)

struct kasan_stack_ring stack_ring = {
	.lock = __RW_LOCK_UNLOCKED(stack_ring.lock)
};

/* kasan.stacktrace=off/on */
static int __init early_kasan_flag_stacktrace(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "off"))
		kasan_arg_stacktrace = KASAN_ARG_STACKTRACE_OFF;
	else if (!strcmp(arg, "on"))
		kasan_arg_stacktrace = KASAN_ARG_STACKTRACE_ON;
	else
		return -EINVAL;

	return 0;
}
early_param("kasan.stacktrace", early_kasan_flag_stacktrace);

/* kasan.stack_ring_size=<number of entries> */
static int __init early_kasan_flag_stack_ring_size(char *arg)
{
	if (!arg)
		return -EINVAL;

	return kstrtoul(arg, 0, &stack_ring.size);
}
early_param("kasan.stack_ring_size", early_kasan_flag_stack_ring_size);

void __init kasan_init_tags(void)
{
	if (kasan_stack_collection_enabled()) {
		if (!stack_ring.size)
			stack_ring.size = KASAN_STACK_RING_SIZE_DEFAULT;
		stack_ring.entries = memblock_alloc(
			sizeof(stack_ring.entries[0]) * stack_ring.size,
			SMP_CACHE_BYTES);
		if (WARN_ON(!stack_ring.entries))
			static_branch_disable(&kasan_flag_stacktrace);
	}
}

void kasan_save_stack_info(struct page *page, u64 op, u64 arg)
{
	unsigned long flags;
	depot_stack_handle_t stack;
	u64 pos;
	struct kasan_stack_ring_entry *entry;

	if (!stack_ring.entries || current->kasan_depth)
  		return;

	++current->kasan_depth;
	stack = kasan_save_stack(GFP_KERNEL, true);

	/*
	 * Prevent save_stack_info() from modifying stack ring
	 * when kasan_complete_mode_report_info() is walking it.
	 */
	read_lock_irqsave(&stack_ring.lock, flags);

	pos = atomic64_fetch_add(1, &stack_ring.pos);
	entry = &stack_ring.entries[pos % stack_ring.size];

	WRITE_ONCE(entry->op, op);
	WRITE_ONCE(entry->arg, arg);
	WRITE_ONCE(entry->stack, stack);

	/*
	 * Paired with smp_load_acquire() in kasan_complete_mode_report_info().
	 */
	smp_store_release(&entry->page, (s64)page);

	read_unlock_irqrestore(&stack_ring.lock, flags);
	--current->kasan_depth;
}
EXPORT_SYMBOL(kasan_save_stack_info);

void kasan_save_alloc_info(struct kmem_cache *cache, void *object, gfp_t flags)
{
}

void kasan_save_free_info(struct kmem_cache *cache, void *object)
{
}
