/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM binder
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_BINDER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BINDER_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct binder_transaction;
struct task_struct;
<<<<<<< HEAD   (029c3b29942fa7c9965736a5ab9030f41c9c02d3 ANDROID: ABI: New variables and hooks added, honor symbol li)
||||||| BASE   (35aeaa5075cc919a17a61214120c743fe2182329 ANDROID: binder: Add vendor hook to the binder)
DECLARE_HOOK(android_vh_binder_transaction_init,
	TP_PROTO(struct binder_transaction *t),
	TP_ARGS(t));
=======
struct binder_thread;
struct binder_proc;
DECLARE_HOOK(android_vh_binder_transaction_init,
	TP_PROTO(struct binder_transaction *t),
	TP_ARGS(t));
>>>>>>> CHANGE (937bd07184e5fe5110f50803ce6a057da9b00342 ANDROID: vendor_hooks: Add hooks for improving binder trans)
DECLARE_HOOK(android_vh_binder_set_priority,
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
DECLARE_HOOK(android_vh_binder_restore_priority,
<<<<<<< HEAD   (029c3b29942fa7c9965736a5ab9030f41c9c02d3 ANDROID: ABI: New variables and hooks added, honor symbol li)
	TP_PROTO(struct binder_transaction *t),
	TP_ARGS(t));
||||||| BASE   (35aeaa5075cc919a17a61214120c743fe2182329 ANDROID: binder: Add vendor hook to the binder)
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
=======
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
DECLARE_HOOK(android_vh_binder_wait_for_work,
	TP_PROTO(bool do_proc_work, struct binder_thread *tsk, struct binder_proc *proc),
	TP_ARGS(do_proc_work, tsk, proc));
DECLARE_HOOK(android_vh_sync_txn_recvd,
	TP_PROTO(struct task_struct *tsk, struct task_struct *from),
	TP_ARGS(tsk, from));
>>>>>>> CHANGE (937bd07184e5fe5110f50803ce6a057da9b00342 ANDROID: vendor_hooks: Add hooks for improving binder trans)
#endif /* _TRACE_HOOK_BINDER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
