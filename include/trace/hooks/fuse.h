/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fuse
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_FUSE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FUSE_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct wait_queue_head;
DECLARE_HOOK(android_vh_queue_request_and_unlock,
	TP_PROTO(struct wait_queue_head *wq_head, bool sync),
	TP_ARGS(wq_head, sync));
DECLARE_HOOK(android_vh_fuse_request_end,
	TP_PROTO(struct task_struct *self),
	TP_ARGS(self));
<<<<<<< HEAD   (5fdbbd61a3cbdca7ac1034dc16723bdbc7ece8cc ANDROID: ABI: pixel: update symbol list)
||||||| BASE   (a5f84b93f0a0194cd78a42283b8bfa679be56093 FROMGIT: Revert "f2fs: block cache/dio write during f2fs_ena)
DECLARE_HOOK(android_vh_fuse_request_end_ext,
	TP_PROTO(struct fuse_req *req, struct task_struct *self),
	TP_ARGS(req, self));
=======
DECLARE_HOOK(android_vh_fuse_request_end_ext,
	TP_PROTO(struct fuse_req *req, struct task_struct *self),
	TP_ARGS(req, self));
DECLARE_HOOK(android_vh_fuse_request_fetch,
	TP_PROTO(struct fuse_req *req, struct task_struct *self),
	TP_ARGS(req, self));
>>>>>>> CHANGE (33d2a795c3f806f90603a2d96a842797c36b5a5a ANDROID: vendor_hooks: Add vendor hooks for fuse)

#endif /* _TRACE_HOOK_FUSE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
