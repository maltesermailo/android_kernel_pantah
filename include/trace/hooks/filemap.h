/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM filemap

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FILEMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FILEMAP_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_io_rbwc,
       TP_PROTO(struct kiocb * iocb, pgoff_t index),
       TP_ARGS(iocb, index));

DECLARE_HOOK(android_vh_io_wbwc,
       TP_PROTO(struct file  *file, unsigned long offset),
       TP_ARGS(file, offset));
#endif /* _TRACE_HOOK_FILEMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

