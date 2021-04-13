/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM logbuf

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_LOGBUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_LOGBUF_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct printk_ringbuffer;
struct printk_record;

DECLARE_HOOK(android_vh_logbuf,
	TP_PROTO(char *buf, u16 len, u64 ts_nsec, u32 caller_id),
	TP_ARGS(buf, len, ts_nsec, caller_id))

DECLARE_HOOK(android_vh_logbuf_pr_cont,
	TP_PROTO(char *buf, u16 len, u8 flags, u16 text_len),
	TP_ARGS(buf, len, flags, text_len))
#else
#define trace_android_vh_logbuf(buf, len, ts_nsec, caller_id)
#define trace_android_vh_logbuf_pr_cont(buf, len, flags, text_len)
#endif

#endif /* _TRACE_HOOK_LOGBUF_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
