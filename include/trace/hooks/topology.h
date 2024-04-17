/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM topology

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_TOPOLOGY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TOPOLOGY_H

#include <trace/hooks/vendor_hooks.h>

struct cpumask;

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_RESTRICTED_HOOK(android_rvh_cpu_capacity_show,
	TP_PROTO(unsigned long *capacity, int cpu),
	TP_ARGS(capacity, cpu), 1);
#else

#define trace_android_rvh_cpu_capacity_show(capacity, cpu)

#endif

#endif /* _TRACE_HOOK_TOPOLOGY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
