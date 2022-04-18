/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmaheapbuf

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMAHEAPBUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMAHEAPBUF_H

#include <trace/hooks/vendor_hooks.h>

struct dma_buf;
DECLARE_HOOK(android_vh_dmaheap_buffer_release,
		TP_PROTO(struct dma_buf *data),
		TP_ARGS(data));
#endif /* _TRACE_HOOK_DMAHEAPBUF_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

