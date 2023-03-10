/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __GZ_ERR_H__
#define __GZ_ERR_H__

/**
 * @brief Definitions of APIs between GenieZone hypervisor and driver
 *
 * These are not needed to be visible to uapi
 */

/* We need GenieZone specific error code in order to map to Linux errno */
#define NO_ERROR                (0)
#define ERR_NO_MEMORY           (-5)
#define ERR_NOT_SUPPORTED       (-24)
#define ERR_NOT_IMPLEMENTED     (-27)
#define ERR_FAULT               (-40)

static inline unsigned int
assemble_vm_vcpu_tuple(gzvm_id_t vmid, gzvm_vcpu_id_t vcpuid)
{
	return ((unsigned int)vmid << 16 | vcpuid);
}

static inline gzvm_id_t get_vmid_from_tuple(unsigned int tuple)
{
	return (gzvm_id_t)(tuple >> 16);
}

static inline gzvm_vcpu_id_t get_vcpuid_from_tuple(unsigned int tuple)
{
	return (gzvm_vcpu_id_t) (tuple & 0xffff);
}

static inline void
disassemble_vm_vcpu_tuple(unsigned int tuple, gzvm_id_t *vmid,
			  gzvm_vcpu_id_t *vcpuid)
{
	*vmid = get_vmid_from_tuple(tuple);
	*vcpuid = get_vcpuid_from_tuple(tuple);
}

/*
 * The following data structures are for data transferring between driver and
 * hypervisor
 */

/* align hypervisor definitions */
#define GZVM_MAX_VCPUS		 8
#define GZVM_MAX_MEM_REGION	10

/* identical to ffa memory constituent */
struct mem_region_addr_range {
	/* The base IPA of the constituent memory region, aligned to 4 kiB */
	__u64 address;
	/* The number of 4 kiB pages in the constituent memory region. */
	__u32 pg_cnt;
	__u32 reserved;
};

struct gzvm_memory_region_ranges {
	__u32 slot;
	__u32 constituent_cnt;
	__u64 total_pages;
	__u64 gpa;
	struct mem_region_addr_range constituents[];
};

#endif /* __GZ_ERR_H__ */
