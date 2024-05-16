/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cmabitmap

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CMA_H

#include <trace/hooks/vendor_hooks.h>
#include <linux/cma.h>

DECLARE_RESTRICTED_HOOK(android_rvh_bitmap_find_best_next_area,
		TP_PROTO(unsigned long *bitmap,
			unsigned long bitmap_maxno,
			unsigned long start,
			unsigned int bitmap_count,
			unsigned long mask,
			unsigned long offset,
			unsigned long *bitmap_no,
			bool status),
		TP_ARGS(bitmap, bitmap_maxno, start, bitmap_count, mask, offset, bitmap_no, status), 1);

#endif /* _TRACE_HOOK_CMA_H */

/* This part must be outside protection */
#undef DEFINE_TRACE_H
#include <trace/define_trace.h>
