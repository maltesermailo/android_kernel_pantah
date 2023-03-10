/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __GZVM_COMMON_H__
#define __GZVM_COMMON_H__

#include <linux/const.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/ptrace.h>

/**
 * @brief This file declares common data structure shared between userspace,
 *        kernel space, and GZ.
 */

enum {
	GZVM_FUNC_CREATE_VM = 0,
	GZVM_FUNC_DESTROY_VM,
	GZVM_FUNC_CREATE_VCPU,
	GZVM_FUNC_DESTROY_VCPU,
	GZVM_FUNC_SET_MEMREGION,
	GZVM_FUNC_RUN,
	GZVM_FUNC_GET_REGS,
	GZVM_FUNC_SET_REGS,
	GZVM_FUNC_GET_ONE_REG,
	GZVM_FUNC_SET_ONE_REG,
	GZVM_FUNC_IRQ_LINE,
	GZVM_FUNC_CREATE_DEVICE,
	GZVM_FUNC_PROBE,
	GZVM_FUNC_ENABLE_CAP,
	NR_GZVM_FUNC
};

typedef __u16 gzvm_id_t;
typedef __u16 gzvm_vcpu_id_t;

/* VM exit reason */
enum {
	GZVM_EXIT_UNKNOWN = 0x92920000,
	GZVM_EXIT_MMIO,
	GZVM_EXIT_HVC,
	GZVM_EXIT_IRQ,
	GZVM_EXIT_EXCEPTION,
	GZVM_EXIT_DEBUG,
	GZVM_EXIT_FAIL_ENTRY,
	GZVM_EXIT_INTERNAL_ERROR,
	GZVM_EXIT_SYSTEM_EVENT,
	GZVM_EXIT_SHUTDOWN,
};

/**
 * @brief same purpose as kvm_run, this struct is shared between userspace,
 *	kernel and GZ
 * Note: keep identical layout between the 3 modules
 */
struct gzvm_vcpu_run {
	/* to userspace */
	__u32 exit_reason;
	__u8 immediate_exit;
	__u8 padding1[3];
	/* union structure of collection of guest exit reason */
	union {
		/* GZVM_EXIT_MMIO */
		struct {
			__u64 phys_addr;		/* from FAR_EL2 */
			__u8 data[8];
			__u64 size;			/* from ESR_EL2 as */
			__u32 reg_nr;			/* from ESR_EL2 */
			__u8 is_write;			/* from ESR_EL2 */
		} mmio;
		/* GZVM_EXIT_FAIL_ENTRY */
		struct {
			__u64 hardware_entry_failure_reason;
			__u32 cpu;
		} fail_entry;
		/* GZVM_EXIT_EXCEPTION */
		struct {
			__u32 exception;
			__u32 error_code;
		} exception;
		/* GZVM_EXIT_HVC */
		struct {
			__u64 args[8];	/* in-out */
		} hvc;
		/* GZVM_EXIT_INTERNAL_ERROR */
		struct {
			__u32 suberror;
			__u32 ndata;
			__u64 data[16];
		} internal;
		/* GZVM_EXIT_SYSTEM_EVENT */
		struct {
#define GZVM_SYSTEM_EVENT_SHUTDOWN       1
#define GZVM_SYSTEM_EVENT_RESET          2
#define GZVM_SYSTEM_EVENT_CRASH          3
#define GZVM_SYSTEM_EVENT_WAKEUP         4
#define GZVM_SYSTEM_EVENT_SUSPEND        5
#define GZVM_SYSTEM_EVENT_SEV_TERM       6
#define GZVM_SYSTEM_EVENT_S2IDLE         7
			__u32 type;
			__u32 ndata;
			__u64 data[16];
		} system_event;
		/* Fix the size of the union. */
		char padding[256];
	};
};

#define GIC_V3_NR_LRS		16

struct gzvm_vcpu_hwstate {
	__u32 nr_lrs;
	__u64 lr[GIC_V3_NR_LRS];
};

/* GZVM ioctls */
#define GZVM_IOC_MAGIC			0x92	/* gz */

/*
 * ioctls for /dev/gzvm fds:
 */
#define GZVM_GET_API_VERSION       _IO(GZVM_IOC_MAGIC,   0x00)
#define GZVM_CREATE_VM             _IO(GZVM_IOC_MAGIC,   0x01)

#define GZVM_CAP_ARM_VM_IPA_SIZE	165
#define GZVM_CAP_ARM_PROTECTED_VM	0xffbadab1

#define GZVM_CHECK_EXTENSION       _IO(GZVM_IOC_MAGIC,   0x03)

/*
 * ioctls for VM fds
 */

/* for GZVM_SET_MEMORY_REGION */
struct gzvm_memory_region {
	__u32 slot;
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size; /* bytes */
};
#define GZVM_SET_MEMORY_REGION     _IOW(GZVM_IOC_MAGIC,  0x40, \
					struct gzvm_memory_region)
/*
 * GZVM_CREATE_VCPU receives as a parameter the vcpu slot, and returns
 * a vcpu fd.
 */
#define GZVM_CREATE_VCPU           _IO(GZVM_IOC_MAGIC,   0x41)

/* for GZVM_SET_USER_MEMORY_REGION */
struct gzvm_userspace_memory_region {
	__u32 slot;
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size; /* bytes */
	__u64 userspace_addr; /* start of the userspace allocated memory */
};
#define GZVM_SET_USER_MEMORY_REGION _IOW(GZVM_IOC_MAGIC, 0x46, \
					struct gzvm_userspace_memory_region)

/* for GZVM_IRQ_LINE */
/* GZVM_IRQ_LINE irq field index values */
#define GZVM_IRQ_VCPU2_SHIFT		28
#define GZVM_IRQ_VCPU2_MASK		0xf
#define GZVM_IRQ_TYPE_SHIFT		24
#define GZVM_IRQ_TYPE_MASK		0xf
#define GZVM_IRQ_VCPU_SHIFT		16
#define GZVM_IRQ_VCPU_MASK		0xff
#define GZVM_IRQ_NUM_SHIFT		0
#define GZVM_IRQ_NUM_MASK		0xffff

/* irq_type field */
#define GZVM_IRQ_TYPE_CPU		0
#define GZVM_IRQ_TYPE_SPI		1
#define GZVM_IRQ_TYPE_PPI		2

/* out-of-kernel GIC cpu interrupt injection irq_number field */
#define GZVM_IRQ_CPU_IRQ		0
#define GZVM_IRQ_CPU_FIQ		1

struct gzvm_irq_level {
	union {
		__u32 irq;
		__s32 status;
	};
	__u32 level;
};
#define GZVM_IRQ_LINE              _IOW(GZVM_IOC_MAGIC,  0x61, \
					struct gzvm_irq_level)

#define GZVM_IRQFD_FLAG_DEASSIGN (1 << 0)
/*
 * GZVM_IRQFD_FLAG_RESAMPLE indicates resamplefd is valid and specifies
 * the irqfd to operate in resampling mode for level triggered interrupt
 * emulation.
 */
#define GZVM_IRQFD_FLAG_RESAMPLE (1 << 1)

struct gzvm_irqfd {
	__u32 fd;
	__u32 gsi;
	__u32 flags;
	__u32 resamplefd;
	__u8  pad[16];
};
#define GZVM_IRQFD                 _IOW(GZVM_IOC_MAGIC,  0x76, \
					struct gzvm_irqfd)

enum {
	gzvm_ioeventfd_flag_nr_datamatch,
	gzvm_ioeventfd_flag_nr_pio,
	gzvm_ioeventfd_flag_nr_deassign,
	gzvm_ioeventfd_flag_nr_max,
};

#define GZVM_IOEVENTFD_FLAG_DATAMATCH (1 << gzvm_ioeventfd_flag_nr_datamatch)
#define GZVM_IOEVENTFD_FLAG_PIO       (1 << gzvm_ioeventfd_flag_nr_pio)
#define GZVM_IOEVENTFD_FLAG_DEASSIGN  (1 << gzvm_ioeventfd_flag_nr_deassign)
#define GZVM_IOEVENTFD_VALID_FLAG_MASK  ((1 << gzvm_ioeventfd_flag_nr_max) - 1)

struct gzvm_ioeventfd {
	__u64 datamatch;
	__u64 addr;        /* legal pio/mmio address */
	__u32 len;         /* 1, 2, 4, or 8 bytes; or 0 to ignore length */
	__s32 fd;
	__u32 flags;
	__u8  pad[36];
};

#define GZVM_IOEVENTFD             _IOW(GZVM_IOC_MAGIC,  0x79, \
					struct gzvm_ioeventfd)

enum gzvm_device_type {
	GZVM_DEV_TYPE_ARM_VGIC_V3_DIST,
	GZVM_DEV_TYPE_ARM_VGIC_V3_REDIST,
	GZVM_DEV_TYPE_MAX,
};

struct gzvm_create_device {
	__u32 dev_type;			/* device type */
	__u32 id;			/* out: device id */
	__u64 flags;			/* device specific flags */
	__u64 dev_addr;			/* device ipa address in VM's view */
	__u64 dev_reg_size;		/* device register range size */
	/*
	 * If user -> kernel, this is user virtual address of device specific
	 * attributes (if needed). If kernel->hypervisor, this is ipa.
	 */
	__u64 attr_addr;
	__u64 attr_size;		/* size of device specific attributes */
};
#define GZVM_CREATE_DEVICE	   _IOWR(GZVM_IOC_MAGIC,  0xe0, \
					struct gzvm_create_device)


/*
 * ioctls for vcpu fds
 */
#define GZVM_RUN                   _IO(GZVM_IOC_MAGIC,   0x80)

/* sub-commands put in args[0] for GZVM_CAP_ARM_PROTECTED_VM */
#define GZVM_CAP_ARM_PVM_SET_PVMFW_IPA		0
#define GZVM_CAP_ARM_PVM_GET_PVMFW_SIZE		1

/* for GZVM_ENABLE_CAP */
struct gzvm_enable_cap {
	/* in */
	__u64 cap;
	/* we have total 5 (8 - 3) registers can be used for additional args */
	__u64 args[5];
};
#define GZVM_ENABLE_CAP            _IOW(GZVM_IOC_MAGIC,  0xa3, \
					struct gzvm_enable_cap)

struct gzvm_one_reg {
	__u64 id;
	__u64 addr;
};
#define GZVM_GET_ONE_REG	   _IOW(GZVM_IOC_MAGIC,  0xab, \
					struct gzvm_one_reg)
#define GZVM_SET_ONE_REG	   _IOW(GZVM_IOC_MAGIC,  0xac, \
					struct gzvm_one_reg)

#define GZVM_REG_ARCH_MASK	0xff00000000000000ULL
#define GZVM_REG_GENERIC	0x0000000000000000ULL

/*
 * Architecture specific registers are to be defined in arch headers and
 * ORed with the arch identifier.
 */
#define GZVM_REG_ARM		0x4000000000000000ULL
#define GZVM_REG_ARM64		0x6000000000000000ULL

#define GZVM_REG_SIZE_SHIFT	52
#define GZVM_REG_SIZE_MASK	0x00f0000000000000ULL
#define GZVM_REG_SIZE_U8	0x0000000000000000ULL
#define GZVM_REG_SIZE_U16	0x0010000000000000ULL
#define GZVM_REG_SIZE_U32	0x0020000000000000ULL
#define GZVM_REG_SIZE_U64	0x0030000000000000ULL
#define GZVM_REG_SIZE_U128	0x0040000000000000ULL
#define GZVM_REG_SIZE_U256	0x0050000000000000ULL
#define GZVM_REG_SIZE_U512	0x0060000000000000ULL
#define GZVM_REG_SIZE_U1024	0x0070000000000000ULL
#define GZVM_REG_SIZE_U2048	0x0080000000000000ULL

#define GZVM_NR_SPSR	5
struct gzvm_regs {
	struct user_pt_regs regs;	/* sp = sp_el0 */

	__u64	sp_el1;
	__u64	elr_el1;

	__u64	spsr[GZVM_NR_SPSR];

	struct user_fpsimd_state fp_regs;
};

/* If you need to interpret the index values, here is the key: */
#define GZVM_REG_ARM_COPROC_MASK	0x000000000FFF0000
#define GZVM_REG_ARM_COPROC_SHIFT	16

/* Normal registers are mapped as coprocessor 16. */
#define GZVM_REG_ARM_CORE		(0x0010 << GZVM_REG_ARM_COPROC_SHIFT)
#define GZVM_REG_ARM_CORE_REG(name)	(offsetof(struct gzvm_regs, name) / sizeof(__u32))

/* Some registers need more space to represent values. */
#define GZVM_REG_ARM_DEMUX		(0x0011 << GZVM_REG_ARM_COPROC_SHIFT)
#define GZVM_REG_ARM_DEMUX_ID_MASK	0x000000000000FF00
#define GZVM_REG_ARM_DEMUX_ID_SHIFT	8
#define GZVM_REG_ARM_DEMUX_ID_CCSIDR	(0x00 << GZVM_REG_ARM_DEMUX_ID_SHIFT)
#define GZVM_REG_ARM_DEMUX_VAL_MASK	0x00000000000000FF
#define GZVM_REG_ARM_DEMUX_VAL_SHIFT	0

/* AArch64 system registers */
#define GZVM_REG_ARM64_SYSREG		(0x0013 << GZVM_REG_ARM_COPROC_SHIFT)
#define GZVM_REG_ARM64_SYSREG_OP0_MASK	0x000000000000c000
#define GZVM_REG_ARM64_SYSREG_OP0_SHIFT	14
#define GZVM_REG_ARM64_SYSREG_OP1_MASK	0x0000000000003800
#define GZVM_REG_ARM64_SYSREG_OP1_SHIFT	11
#define GZVM_REG_ARM64_SYSREG_CRN_MASK	0x0000000000000780
#define GZVM_REG_ARM64_SYSREG_CRN_SHIFT	7
#define GZVM_REG_ARM64_SYSREG_CRM_MASK	0x0000000000000078
#define GZVM_REG_ARM64_SYSREG_CRM_SHIFT	3
#define GZVM_REG_ARM64_SYSREG_OP2_MASK	0x0000000000000007
#define GZVM_REG_ARM64_SYSREG_OP2_SHIFT	0

/* Physical Timer EL0 Registers */
#define GZVM_REG_ARM_PTIMER_CTL		ARM64_SYS_REG(3, 3, 14, 2, 1)
#define GZVM_REG_ARM_PTIMER_CVAL	ARM64_SYS_REG(3, 3, 14, 2, 2)
#define GZVM_REG_ARM_PTIMER_CNT		ARM64_SYS_REG(3, 3, 14, 0, 1)

/* SVE registers */
#define GZVM_REG_ARM64_SVE		(0x0015 << KVM_REG_ARM_COPROC_SHIFT)

#endif /* __GZVM_COMMON_H__ */
