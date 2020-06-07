// SPDX-License-Identifier: GPL-2.0
/*
 * Register read and write tracepoints
 *
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/iorw.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rwio.h>

#ifdef CONFIG_TRACEPOINTS
void __log_write_io(volatile void __iomem *addr)
{
	trace_rwio_write(CALLER_ADDR0, addr);
}
EXPORT_SYMBOL_GPL(__log_write_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_write);

void __log_read_io(const volatile void __iomem *addr)
{
	trace_rwio_read(CALLER_ADDR0, addr);
}
EXPORT_SYMBOL_GPL(__log_read_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_read);
#endif
