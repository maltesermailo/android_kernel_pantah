// SPDX-License-Identifier: GPL-2.0-only
/*
 * pKVM variant of set_memory_region_test.
 *
 * Mirrored cases:
 *  - test_zero_memory_regions
 *  - test_mmio_during_vectoring
 *  - test_invalid_memory_region_flags
 *  - test_add_max_memory_regions
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kvm.h>

#include "kvm_util.h"
#include "pkvm/pkvm_util.h"
#include "processor.h"
#include "test_util.h"

#define MEM_REGION_SIZE 0x200000
#define MEM_REGION_GPA  0xc0000000
#define MEM_REGION_SLOT 10

static void run_expect_efault(struct kvm_vcpu *vcpu)
{
	int r;

	do {
		r = __vcpu_run(vcpu);
	} while (r == -1 && errno == EINTR);

	TEST_ASSERT(r == -1 && errno == EFAULT,
		    "KVM_RUN should fail with EFAULT, ret=%d errno=%d", r, errno);
}

static void test_zero_memory_regions(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	pr_info("Testing KVM_RUN with zero added memory regions (pKVM)\n");

	vm = vm_create_barebones_type(KVM_X86_PKVM_PROTECTED_VM);
	vcpu = __vm_vcpu_add(vm, 0);

	vm_ioctl(vm, KVM_SET_NR_MMU_PAGES, (void *)64ul);
	run_expect_efault(vcpu);

	kvm_vm_free(vm);
}

static void guest_code_mmio_during_vectoring(void)
{
	const struct desc_ptr idt_desc = {
		.address = MEM_REGION_GPA,
		.size = 0xFFF,
	};

	set_idt(&idt_desc);

	/* Generate a #GP by dereferencing a non-canonical address. */
	*((uint8_t *)NONCANONICAL) = 0x1;

	GUEST_ASSERT(0);
}

/*
 * In pKVM protected VM mode this path should fail KVM_RUN with -EFAULT.
 */
static void test_mmio_during_vectoring(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	pr_info("Testing MMIO during vectoring handling in pKVM\n");

	vm = vm_create_shape_with_one_vcpu(VM_SHAPE_PKVM_PROTECTED, &vcpu,
					   guest_code_mmio_during_vectoring);
	virt_map(vm, MEM_REGION_GPA, MEM_REGION_GPA, 1);

	run_expect_efault(vcpu);

	kvm_vm_free(vm);
}

static void test_invalid_memory_region_flags(void)
{
	uint32_t supported_flags = KVM_MEM_LOG_DIRTY_PAGES;
	const uint32_t v2_only_flags = KVM_MEM_GUEST_MEMFD;
	struct kvm_vm *vm;
	int r, i;

	supported_flags |= KVM_MEM_READONLY;

	vm = vm_create_barebones_type(KVM_X86_PKVM_PROTECTED_VM);

	/* pKVM does not support dirty logging on x86. */
	r = __vm_set_user_memory_region(vm, 0, KVM_MEM_LOG_DIRTY_PAGES,
					0, MEM_REGION_SIZE, NULL);
	TEST_ASSERT(r && errno == EINVAL,
		    "KVM_SET_USER_MEMORY_REGION should fail with KVM_MEM_LOG_DIRTY_PAGES on pKVM");

	r = __vm_set_user_memory_region2(vm, 0, KVM_MEM_LOG_DIRTY_PAGES,
					 0, MEM_REGION_SIZE, NULL, 0, 0);
	TEST_ASSERT(r && errno == EINVAL,
		    "KVM_SET_USER_MEMORY_REGION2 should fail with KVM_MEM_LOG_DIRTY_PAGES on pKVM");

	if (kvm_check_cap(KVM_CAP_MEMORY_ATTRIBUTES) & KVM_MEMORY_ATTRIBUTE_PRIVATE)
		supported_flags |= KVM_MEM_GUEST_MEMFD;

	for (i = 0; i < 32; i++) {
		if ((supported_flags & BIT(i)) && !(v2_only_flags & BIT(i)))
			continue;

		r = __vm_set_user_memory_region(vm, 0, BIT(i),
						0, MEM_REGION_SIZE, NULL);

		TEST_ASSERT(r && errno == EINVAL,
			    "KVM_SET_USER_MEMORY_REGION should fail on v2-only flag 0x%lx",
			    BIT(i));

		if (supported_flags & BIT(i))
			continue;

		r = __vm_set_user_memory_region2(vm, 0, BIT(i),
						 0, MEM_REGION_SIZE, NULL, 0, 0);
		TEST_ASSERT(r && errno == EINVAL,
			    "KVM_SET_USER_MEMORY_REGION2 should fail on unsupported flag 0x%lx",
			    BIT(i));
	}

	if (supported_flags & KVM_MEM_GUEST_MEMFD) {
		int guest_memfd = vm_create_guest_memfd(vm, MEM_REGION_SIZE, 0);

		r = __vm_set_user_memory_region2(vm, 0,
						 KVM_MEM_LOG_DIRTY_PAGES | KVM_MEM_GUEST_MEMFD,
						 0, MEM_REGION_SIZE, NULL, guest_memfd, 0);
		TEST_ASSERT(r && errno == EINVAL,
			    "KVM_SET_USER_MEMORY_REGION2 should fail, dirty logging private memory is unsupported");

		r = __vm_set_user_memory_region2(vm, 0,
						 KVM_MEM_READONLY | KVM_MEM_GUEST_MEMFD,
						 0, MEM_REGION_SIZE, NULL, guest_memfd, 0);
		TEST_ASSERT(r && errno == EINVAL,
			    "KVM_SET_USER_MEMORY_REGION2 should fail, read-only GUEST_MEMFD memslots are unsupported");

		close(guest_memfd);
	}

	kvm_vm_free(vm);
}

/*
 * Verify adding memslots up to KVM_CAP_NR_MEMSLOTS succeeds, and adding one
 * more fails with EINVAL.
 */
static void test_add_max_memory_regions(void)
{
	int ret;
	struct kvm_vm *vm;
	uint32_t max_mem_slots;
	uint32_t slot;
	void *mem, *mem_extra;

	max_mem_slots = kvm_check_cap(KVM_CAP_NR_MEMSLOTS);
	TEST_ASSERT(max_mem_slots > 0,
		    "KVM_CAP_NR_MEMSLOTS should be greater than 0");
	pr_info("Allowed number of memory slots: %i\n", max_mem_slots);

	vm = vm_create_barebones_type(KVM_X86_PKVM_PROTECTED_VM);

	pr_info("Adding slots 0..%i, each memory region with %dK size\n",
		(max_mem_slots - 1), MEM_REGION_SIZE >> 10);

	mem = kvm_mmap((size_t)max_mem_slots * MEM_REGION_SIZE,
		       PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1);

	for (slot = 0; slot < max_mem_slots; slot++)
		vm_set_user_memory_region(vm, slot, 0,
					  ((uint64_t)slot * MEM_REGION_SIZE),
					  MEM_REGION_SIZE,
					  mem + (uint64_t)slot * MEM_REGION_SIZE);

	mem_extra = kvm_mmap(MEM_REGION_SIZE, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1);

	ret = __vm_set_user_memory_region(vm, max_mem_slots, 0,
					  (uint64_t)max_mem_slots * MEM_REGION_SIZE,
					  MEM_REGION_SIZE, mem_extra);
	TEST_ASSERT(ret == -1 && errno == EINVAL,
		    "Adding one more memory slot should fail with EINVAL");

	kvm_munmap(mem, (size_t)max_mem_slots * MEM_REGION_SIZE);
	kvm_munmap(mem_extra, MEM_REGION_SIZE);
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_PKVM_PROTECTED_VM));

	test_zero_memory_regions();
	test_mmio_during_vectoring();
	test_invalid_memory_region_flags();
	test_add_max_memory_regions();

	return 0;
}
