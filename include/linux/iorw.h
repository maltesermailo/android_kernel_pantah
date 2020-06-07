/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 */
#ifndef __LOG_IORW_H__
#define __LOG_IORW_H__

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/tracepoint-defs.h>

#if IS_ENABLED(CONFIG_TRACE_RW)
DECLARE_TRACEPOINT(rwio_write);
DECLARE_TRACEPOINT(rwio_read);

void __log_write_io(volatile void __iomem *addr);
void __log_read_io(const volatile void __iomem *addr);

#define log_write_io(addr)			\
do {						\
	if (tracepoint_enabled(rwio_write))	\
		__log_write_io(addr);		\
} while (0)

#define log_read_io(addr)			\
do {						\
	if (tracepoint_enabled(rwio_read))	\
		__log_read_io(addr);		\
} while (0)

#else
static inline void log_write_io(volatile void __iomem *addr)
{ }
static inline void log_read_io(const volatile void __iomem *addr)
{ }
#endif /* CONFIG_TRACE_RW */

#endif /* __LOG_IORW_H__  */
