#if !defined(__PKVM_SMC_FILTER_HYPEVENTS_H_) || defined(HYP_EVENT_MULTI_READ)
#define __PKVM_SMC_FILTER_HYPEVENTS_H_

#ifdef __KVM_NVHE_HYPERVISOR__
#include <trace.h>
#endif

HYP_EVENT(filtered_smc,
	HE_PROTO(u64 smc_id, u64 x1, u64 x2, u64 x3, u64 x4, u64 x5, u64 x6, u64 x7),
	HE_STRUCT(
		he_field(u64, smc_id)
		he_field(u64, x1)
		he_field(u64, x2)
		he_field(u64, x3)
		he_field(u64, x4)
		he_field(u64, x5)
		he_field(u64, x6)
		he_field(u64, x7)
	),
	HE_ASSIGN(
		__entry->smc_id = smc_id;
		__entry->x1 = x1;
		__entry->x2 = x2;
		__entry->x3 = x3;
		__entry->x4 = x4;
		__entry->x5 = x5;
		__entry->x6 = x6;
		__entry->x7 = x7;
	),
	HE_PRINTK("smc_id=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx",
		  __entry->smc_id, __entry->x1, __entry->x2, __entry->x3,
		  __entry->x4, __entry->x5, __entry->x6, __entry->x7)
);
#endif
