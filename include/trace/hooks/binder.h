/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM binder

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BINDER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BINDER_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct binder_transaction;
struct binder_thread;
DECLARE_HOOK(android_vh_binder_set_tune,
	TP_PROTO(struct binder_transaction *t, struct binder_thread *thread),
	TP_ARGS(t, thread));
DECLARE_HOOK(android_vh_binder_restore_tune,
	TP_PROTO(struct binder_transaction *t, struct binder_thread *thread),
	TP_ARGS(t, thread));
#else
#define trace_android_rvh_binder_set_tune(t, thread)
#define trace_android_rvh_binder_restore_tune(t, thread)
#endif

#endif /* _TRACE_HOOK_BINDER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
