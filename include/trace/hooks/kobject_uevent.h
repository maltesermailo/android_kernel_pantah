/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kobject_uevent

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_KOBJECT_UEVENT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_KOBJECT_UEVENT_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_log_uevent,
	TP_PROTO(const char *devpath, unsigned int action),
	TP_ARGS(devpath, action));

#endif /* _TRACE_HOOK_KOBJECT_UEVENT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
