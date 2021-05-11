/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmalloc
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_VMALLOC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SIGNAL_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
#endif /* _TRACE_HOOK_SIGNAL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
