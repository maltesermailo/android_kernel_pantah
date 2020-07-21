/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwsem

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_RWSEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_RWSEM_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

/*
 *  * Following tracepoints are not exported in tracefs and provide a
 *   * mechanism for vendor modules to hook and extend functionality
 *    */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct rw_semaphore;
struct rwsem_waiter;

DECLARE_HOOK(android_vh_rwsem_set_tune,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_restore_tune,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_alter_rwsem_list_add,
	TP_PROTO(struct rwsem_waiter *waiter,
		 struct rw_semaphore *sem,
		 bool *do_alter),
	TP_ARGS(waiter, sem, do_alter));
#else
#define trace_android_rvh_rwsem_set_tune(sem)
#define trace_android_rvh_rwsem_retore_tune(sem)
#define trace_android_rvh_alter_rwsem_list_add(waiter, sem, do_alter)
#endif

#endif /* _TRACE_HOOK_RWSEM_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
