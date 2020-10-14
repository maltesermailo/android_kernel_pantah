/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM debug

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DEBUG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DEBUG_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct pt_regs;

DECLARE_HOOK(android_vh_ipi_stop,
	TP_PROTO(struct pt_regs *regs),
	TP_ARGS(regs))

DECLARE_HOOK(android_vh_softirq_entry,
	TP_PROTO(unsigned int vec_nr, void *action),
	TP_ARGS(vec_nr, action))

DECLARE_HOOK(android_vh_softirq_exit,
	TP_PROTO(unsigned int vec_nr, void *action),
	TP_ARGS(vec_nr, action))

struct irq_desc;

DECLARE_HOOK(android_vh_irq_entry,
	TP_PROTO(int irq, struct irq_desc *desc,
		unsigned long long start_time),
	TP_ARGS(irq, desc, start_time))

DECLARE_HOOK(android_vh_irq_exit,
	TP_PROTO(int irq, struct irq_desc *desc,
		unsigned long long start_time),
	TP_ARGS(irq, desc, start_time))

DECLARE_HOOK(android_vh_smp_call_entry,
	TP_PROTO(void *func),
	TP_ARGS(func))

DECLARE_HOOK(android_vh_smp_call_exit,
	TP_PROTO(void *func),
	TP_ARGS(func))
#else
#define trace_android_vh_ipi_stop(regs)
#define trace_android_vh_softirq_entry(vec_nr, action)
#define trace_android_vh_softirq_exit(vec_nr, action)
#define trace_android_vh_irq_entry(irq, desc, start_time)
#define trace_android_vh_irq_exit(irq, desc, start_time)
#define trace_android_vh_smp_call_entry(func)
#define trace_android_vh_smp_call_exit(func)
#endif

#endif /* _TRACE_HOOK_DEBUG_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
