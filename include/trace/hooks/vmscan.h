/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_set_balance_anon_file_reclaim,
			TP_PROTO(bool *balance_anon_file_reclaim),
			TP_ARGS(balance_anon_file_reclaim), 1);
<<<<<<< HEAD   (a527dc6296086cbd0e329c8881d5fd2e1e5b0222 ANDROID: ABI: add initial symbol list for exynos)
||||||| BASE   (d28ab9d5f400994d12dc5b2ebd5812a683fa7779 ANDROID: GKI: net: add oem data for rtt statistics.)
DECLARE_HOOK(android_vh_check_folio_look_around_ref,
	TP_PROTO(struct folio *folio, int *skip),
	TP_ARGS(folio, skip));
=======
DECLARE_HOOK(android_vh_check_folio_look_around_ref,
	TP_PROTO(struct folio *folio, int *skip),
	TP_ARGS(folio, skip));
DECLARE_HOOK(android_vh_tune_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));
>>>>>>> CHANGE (ed42257de1936449c72615cda997d1ffca113bd4 ANDROID: vendor_hooks: Add tune swappiness hook in get_scan_)
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
