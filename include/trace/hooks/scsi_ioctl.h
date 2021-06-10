/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scsi_ioctl
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SCSI_IOCTL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCSI_IOCTL_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct blk_cmd_filter;
struct request_queue;

DECLARE_HOOK(android_vh_scsi_filter_cmd,
	TP_PROTO(struct blk_cmd_filter *filter),
	TP_ARGS(filter));

DECLARE_HOOK(android_vh_scsi_vendor_cbd,
	TP_PROTO(struct request_queue *q, int opcode, int *cmdlen),
	TP_ARGS(q, opcode, cmdlen));

#endif /* _TRACE_HOOK_SCSI_IOCTL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
