// SPDX-License-Identifier: GPL-2.0-only
/*
 * Guest-side debug register tests for pKVM protected VM.
 */

#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_util.h"

#define DR6_B0		BIT_ULL(0)   /* DR0 matched (DR6_TRAP0) */
#define DR6_BD		BIT_ULL(13)  /* General Detect status */
#define DR6_BS		BIT_ULL(14)  /* Single-step status */

#define DR7_GD		BIT_ULL(13)  /* General Detect enable */
#define DR7_LE		BIT_ULL(8)   /* Local Exact enable */
#define DR7_L0		BIT_ULL(0)   /* Local enable for DR0 */
#define DR7_RW0_WRITE	BIT_ULL(16) /* DR0 breakpoint type: write */
#define DR7_LEN0_4B	(BIT_ULL(18) | BIT_ULL(19)) /* DR0 len: 4-byte */

#define X86_EFLAGS_RF	BIT_ULL(16)

static volatile uint32_t guest_value;

static volatile uint64_t bp_count; /* Number of #BP exceptions observed. */
static volatile uint64_t bp_rip;

static volatile uint64_t db_count; /* Total number of #DB exceptions observed. */
static volatile uint64_t db_b0_count; /* #DB events with DR6.B0 (DR0 match). */
static volatile uint64_t db_bs_count; /* #DB events with DR6.BS (single-step). */
static volatile uint64_t db_bd_count; /* #DB events with DR6.BD (general detect). */
static volatile uint64_t db_last_rip;
static volatile uint64_t db_last_dr6;

extern unsigned char sw_bp_after;
extern unsigned char hw_bp;
extern unsigned char write_data;
extern unsigned char bd_start;

static inline void write_dr0(uint64_t val)
{
	asm volatile("mov %0, %%dr0" : : "r"(val) : "memory");
}

static inline void write_dr6(uint64_t val)
{
	asm volatile("mov %0, %%dr6" : : "r"(val) : "memory");
}

static inline void write_dr7(uint64_t val)
{
	asm volatile("mov %0, %%dr7" : : "r"(val) : "memory");
}

static inline uint64_t read_dr6(void)
{
	uint64_t val;

	asm volatile("mov %%dr6, %0" : "=r"(val));
	return val;
}

static void guest_bp_handler(struct ex_regs *regs)
{
	bp_count++;
	bp_rip = regs->rip;
}

static void guest_db_handler(struct ex_regs *regs)
{
	uint64_t dr6 = read_dr6();

	db_count++;
	db_last_rip = regs->rip;
	db_last_dr6 = dr6;

	if (dr6 & DR6_B0) {
		db_b0_count++;
		/* Resume flag avoids retriggering instruction breakpoints. */
		regs->rflags |= X86_EFLAGS_RF;
	}

	if (dr6 & DR6_BS)
		db_bs_count++;

	if (dr6 & DR6_BD)
		db_bd_count++;

	write_dr6(0);
}

static void reset_db_state(void)
{
	db_count = 0;
	db_b0_count = 0;
	db_bs_count = 0;
	db_bd_count = 0;
	db_last_rip = 0;
	db_last_dr6 = 0;
	write_dr6(0);
}

static void guest_main(void)
{
	uint64_t old_rflags;

	/* Software breakpoint: #BP is handled entirely in guest. */
	asm volatile(
		"int3\n\t"
		".global sw_bp_after\n\t"
		"sw_bp_after:\n\t");

	GUEST_ASSERT_EQ(bp_count, 1);
	GUEST_ASSERT_EQ(bp_rip, (uint64_t)(uintptr_t)&sw_bp_after);

	/* Instruction hardware breakpoint via DR0. */
	reset_db_state();
	write_dr0((uint64_t)(uintptr_t)&hw_bp);
	write_dr7(DR7_LE | DR7_L0);

	asm volatile(
		".global hw_bp\n\t"
		"hw_bp: nop\n\t");

	write_dr7(0);
	GUEST_ASSERT_EQ(db_count, 1);
	GUEST_ASSERT_EQ(db_b0_count, 1);
	GUEST_ASSERT_EQ(db_last_rip, (uint64_t)(uintptr_t)&hw_bp);

	/* Data write watchpoint via DR0. */
	reset_db_state();
	write_dr0((uint64_t)(uintptr_t)&guest_value);
	write_dr7(DR7_LE | DR7_L0 | DR7_RW0_WRITE | DR7_LEN0_4B);

	asm volatile(
		".global write_data\n\t"
		"write_data: movl $0x12345678, %0\n\t"
		: "=m"(guest_value)
		:
		: "memory");

	write_dr7(0);
	GUEST_ASSERT_EQ(db_count, 1);
	GUEST_ASSERT_EQ(db_b0_count, 1);
	GUEST_ASSERT_EQ(guest_value, 0x12345678);

	/* Single-step test with TF set in-guest, no host debug controls. */
	reset_db_state();
	asm volatile(
		"pushfq\n\t"
		"popq %0\n\t"
		"orq $0x100, %0\n\t"
		"pushq %0\n\t"
		"popfq\n\t"
		"nop\n\t"
		"nop\n\t"
		"pushfq\n\t"
		"popq %0\n\t"
		"andq $~0x100, %0\n\t"
		"pushq %0\n\t"
		"popfq\n\t"
		: "=&r"(old_rflags)
		:
		: "cc", "memory");

	GUEST_ASSERT(db_bs_count >= 2);

	/* DR7.GD triggers #DB with DR6.BD when accessing debug registers. */
	reset_db_state();
	write_dr7(DR7_LE | DR7_GD);

	asm volatile(
		".global bd_start\n\t"
		"bd_start: mov %%dr0, %%rax\n\t"
		:
		:
		: "rax", "memory");

	GUEST_ASSERT_EQ(db_count, 1);
	GUEST_ASSERT_EQ(db_bd_count, 1);
	GUEST_ASSERT_EQ(db_last_rip, (uint64_t)(uintptr_t)&bd_start);

	write_dr7(0);

	GUEST_DONE();
}

int main(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct ucall uc;

	TEST_REQUIRE(is_pkvm_enabled());

	vm = vm_create_shape_with_one_vcpu(VM_SHAPE_PKVM_PROTECTED, &vcpu,
					   guest_main);

	vm_install_exception_handler(vm, BP_VECTOR, guest_bp_handler);
	vm_install_exception_handler(vm, DB_VECTOR, guest_db_handler);

	vcpu_run(vcpu);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unexpected exit: %s", exit_reason_str(vcpu->run->exit_reason));
	}

	kvm_vm_free(vm);
	return 0;
}