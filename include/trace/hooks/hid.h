/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hid
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_HID_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_HID_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct hid_device;
struct hid_report;
DECLARE_HOOK(android_vh_hid_input_report,
	TP_PROTO(struct hid_device *hdev, struct hid_report *report, u8 *data, int size),
	TP_ARGS(hdev, report, data, size));
#endif

#include <trace/define_trace.h>
