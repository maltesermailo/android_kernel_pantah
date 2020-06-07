/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 */
#ifndef __LOG_IORW_H__
#define __LOG_IORW_H__

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/tracepoint-defs.h>

#if IS_ENABLED(CONFIG_TRACE_RW) && IS_ENABLED(__FTRACE_ENABLED_HERE__)
DECLARE_TRACEPOINT(rwio_write);
DECLARE_TRACEPOINT(rwio_read);
DECLARE_TRACEPOINT(rwio_post_read);

void __log_write_io(u64 val, u8 width, volatile void __iomem *addr);
void __log_read_io(const volatile void __iomem *addr);
void __log_post_read_io(u64 val, u8 width, const volatile void __iomem *addr);

#define log_write_io(val, width, addr)			\
do {							\
	if (tracepoint_enabled(rwio_write))		\
		__log_write_io(val, width, addr);	\
} while (0)

#define log_read_io(addr)				\
do {							\
	if (tracepoint_enabled(rwio_read))		\
		__log_read_io(addr);			\
} while (0)

#define log_post_read_io(val, width, addr)		\
do {							\
	if (tracepoint_enabled(rwio_post_read))	\
		__log_post_read_io(val, addr);		\
} while (0)

#else
static inline void log_write_io(u64 val, u8 width, volatile void __iomem *addr)
{ }
static inline void log_read_io(const volatile void __iomem *addr)
{ }
static inline void log_post_read_io(u64 val, u8 width, const volatile void __iomem *addr)
{ }
#endif /* CONFIG_TRACE_RW */

#endif /* __LOG_IORW_H__  */
