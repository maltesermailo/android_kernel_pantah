// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simplified pKVM variant of mmu_stress_test.
 *
 * Only exercise vCPU spawn and initial memory touching over a single large
 * memslot, as x86 pKVM cannot alias one host backing region across multiple
 * memslots in the same way as the generic stress test.
 */

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/atomic.h>
#include <linux/kvm_para.h>
#include <linux/sizes.h>

#include "asm/kvm.h"
#include "kvm_util.h"
#include "pkvm/pkvm_boot.h"
#include "pkvm/pkvm_util.h"
#include "processor.h"
#include "test_util.h"
#include "ucall_common.h"

#define MEM_REGION_GPA SZ_4G
#define MEM_REGION_SLOT 1
#define DEFAULT_MEM_HEADROOM SZ_1G
#define DEFAULT_MEM_RESERVE_DIVISOR 5

enum {
	UCALL_AP_STARTED = 1,
	UCALL_VCPU_TOUCHED,
	UCALL_AP_START_FAIL,
};

static uint64_t guest_nr_vcpus;

struct vcpu_thread_args {
	struct kvm_vcpu *vcpu;
	uint32_t vcpu_id;
};

static pthread_mutex_t ap_start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ap_start_cond = PTHREAD_COND_INITIALIZER;
static uint32_t nr_test_vcpus;
static bool *ap_can_run;
static atomic_t rendezvous;
static uint32_t nr_aps_started;
static bool ap_bringup_timed;
static struct timespec ap_bringup_done;

static uint64_t get_default_max_mem(bool hugepages)
{
	const char *mem_available = "MemAvailable:";
	const char *hugepages_free = "HugePages_Free:";
	char buf[128];
	unsigned long long available_kb = 0;
	unsigned long long free_hugepages = 0;
	uint64_t available_mem;
	uint64_t reserve_mem;
	uint64_t align = PG_SIZE_2M;
	uint64_t max_mem;
	FILE *f;

	if (hugepages)
		align = max_t(uint64_t, align, get_def_hugetlb_pagesz());

	f = fopen("/proc/meminfo", "r");
	TEST_ASSERT(f != NULL, "Error opening /proc/meminfo");

	while (fgets(buf, sizeof(buf), f) != NULL) {
		if (!available_kb && strstr(buf, mem_available) == buf)
			available_kb = strtoull(buf + strlen(mem_available), NULL, 10);

		if (hugepages && !free_hugepages &&
		    strstr(buf, hugepages_free) == buf)
			free_hugepages = strtoull(buf + strlen(hugepages_free), NULL, 10);
	}

	TEST_ASSERT(!fclose(f), "fclose(/proc/meminfo) failed");

	if (hugepages) {
		max_mem = free_hugepages * align;
		TEST_REQUIRE(max_mem >= align);
	} else {
		TEST_ASSERT(available_kb, "MemAvailable missing in /proc/meminfo");
		available_mem = available_kb << 10;
		/*
		 * Leave a minimum 1 GiB cushion and keep roughly 20% of
		 * MemAvailable in reserve so the default run is large, but does not
		 * consume nearly all host RAM.
		 */
		reserve_mem = max_t(uint64_t, DEFAULT_MEM_HEADROOM,
					   available_mem / DEFAULT_MEM_RESERVE_DIVISOR);
		max_mem = available_mem > reserve_mem ?
			available_mem - reserve_mem : 0;
	}

	max_mem &= ~(align - 1);
	TEST_ASSERT(max_mem >= align,
		    "Not enough free memory for a %lu-byte memslot",
		    (unsigned long)align);

	return max_mem;
}

static void calc_default_nr_vcpus(void)
{
	cpu_set_t possible_mask;
	int ret;

	ret = sched_getaffinity(0, sizeof(possible_mask), &possible_mask);
	TEST_ASSERT(!ret, "sched_getaffinity failed, errno = %d (%s)",
		    errno, strerror(errno));

	nr_test_vcpus = CPU_COUNT(&possible_mask) * 3 / 4;
	TEST_ASSERT(nr_test_vcpus > 0, "Uh, no CPUs?");
}

static void rendezvous_with_boss(void)
{
	int orig = atomic_read(&rendezvous);

	if (orig > 0) {
		atomic_dec_and_test(&rendezvous);
		while (atomic_read(&rendezvous) > 0)
			cpu_relax();
	} else {
		atomic_inc(&rendezvous);
		while (atomic_read(&rendezvous) < 0)
			cpu_relax();
	}
}

static void rendezvous_with_vcpus(struct timespec *time, const char *name)
{
	int i;
	int rendezvoused;

	pr_info("Waiting for vCPUs to finish %s...\n", name);

	rendezvoused = atomic_read(&rendezvous);
	for (i = 0; abs(rendezvoused) != 1; i++) {
		usleep(100);
		if (!(i & 0x3f))
			pr_info("\r%d vCPUs haven't rendezvoused...",
				abs(rendezvoused) - 1);
		rendezvoused = atomic_read(&rendezvous);
	}

	clock_gettime(CLOCK_MONOTONIC, time);

	pr_info("\rAll vCPUs finished %s, releasing...\n", name);
	if (rendezvoused > 0)
		atomic_set(&rendezvous, -nr_test_vcpus - 1);
	else
		atomic_set(&rendezvous, nr_test_vcpus + 1);
}

static void check_create_thread(pthread_t *thread, void *(*func)(void *),
				void *data)
{
	int ret;

	ret = pthread_create(thread, NULL, func, data);
	TEST_ASSERT(!ret, "pthread_create failed, ret=%d", ret);
}

static void guest_touch_range(uint64_t start_gpa, uint64_t end_gpa,
			      uint64_t stride)
{
	uint64_t gpa;

	for (gpa = start_gpa; gpa < end_gpa; gpa += stride)
		vcpu_arch_put_guest(*((volatile uint64_t *)gpa), gpa);
}

static void guest_code_ap(uint64_t start_gpa, uint64_t end_gpa, uint64_t stride)
{
	guest_touch_range(start_gpa, end_gpa, stride);
	GUEST_SYNC(UCALL_VCPU_TOUCHED);
	GUEST_DONE();
}

static void guest_code_bsp(uint64_t start_gpa, uint64_t end_gpa,
			   uint64_t stride)
{
	uint32_t apic_id;

	for (apic_id = 1; apic_id < guest_nr_vcpus; apic_id++) {
		long ret = kvm_hypercall(PKVM_GHC_START_CPU, apic_id,
					 PKVM_AP_SIPI_BOOT_GPA, 0, 0);

		if (ret) {
			GUEST_SYNC_ARGS(UCALL_AP_START_FAIL, apic_id, ret, 0, 0);
			GUEST_DONE();
		}

		GUEST_SYNC_ARGS(UCALL_AP_STARTED, apic_id, 0, 0, 0);
	}

	guest_touch_range(start_gpa, end_gpa, stride);
	GUEST_SYNC(UCALL_VCPU_TOUCHED);
	GUEST_DONE();
}

static void *run_vcpu(void *data)
{
	struct vcpu_thread_args *thread_args = data;
	struct kvm_vcpu *vcpu = thread_args->vcpu;
	struct ucall uc;
	bool saw_touch_sync = false;

	if (thread_args->vcpu_id) {
		pthread_mutex_lock(&ap_start_lock);
		while (!ap_can_run[thread_args->vcpu_id])
			pthread_cond_wait(&ap_start_cond, &ap_start_lock);
		pthread_mutex_unlock(&ap_start_lock);
	}

	for (;;) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			switch (uc.args[1]) {
			case UCALL_AP_STARTED:
				TEST_ASSERT(thread_args->vcpu_id == 0,
					    "Only the BSP should report AP start");
				TEST_ASSERT(uc.args[2] > 0 && uc.args[2] < nr_test_vcpus,
					    "Invalid AP id %lu", uc.args[2]);

				pthread_mutex_lock(&ap_start_lock);
				nr_aps_started++;
				if (!ap_bringup_timed && nr_aps_started == nr_test_vcpus - 1) {
					clock_gettime(CLOCK_MONOTONIC, &ap_bringup_done);
					ap_bringup_timed = true;
				}
				ap_can_run[uc.args[2]] = true;
				pthread_cond_broadcast(&ap_start_cond);
				pthread_mutex_unlock(&ap_start_lock);
				continue;
			case UCALL_VCPU_TOUCHED:
				saw_touch_sync = true;
				rendezvous_with_boss();
				continue;
			case UCALL_AP_START_FAIL:
				TEST_FAIL("PKVM_GHC_START_CPU failed for vCPU %lu, rc=%lu",
					  uc.args[2], uc.args[3]);
			default:
				TEST_FAIL("Unexpected sync stage %lu for vCPU %u",
					  uc.args[1], thread_args->vcpu_id);
			}
		case UCALL_DONE:
			TEST_ASSERT(saw_touch_sync,
				    "vCPU %u reached DONE without reporting touched range",
				    thread_args->vcpu_id);
			return NULL;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			TEST_FAIL("Unexpected ucall %lu for vCPU %u, exit_reason=%s",
				  uc.cmd, thread_args->vcpu_id,
				  exit_reason_str(vcpu->run->exit_reason));
		}
	}
}

int main(int argc, char *argv[])
{
	struct vcpu_thread_args *thread_args;
	struct kvm_vcpu **vcpus;
	struct kvm_vm *vm;
	pthread_t *threads;
	struct timespec spawn_start;
	struct timespec spawn_done;
	struct timespec spawn_elapsed;
	struct timespec ap_bringup_elapsed;
	enum vm_mem_backing_src_type backing_src = VM_MEM_SRC_ANONYMOUS;
	void *mem;
	uint64_t offset;
	uint64_t max_mem = 0;
	uint64_t pages_per_vcpu;
	uint64_t start_gpa;
	uint64_t end_gpa;
	uint32_t vcpu_id;
	int opt;
	bool hugepages = false;

	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE((MEM_REGION_GPA % PG_SIZE_2M) == 0);
	calc_default_nr_vcpus();

	while ((opt = getopt(argc, argv, "c:m:Hh")) != -1) {
		switch (opt) {
		case 'c':
			nr_test_vcpus = atoi_positive("Number of vCPUs", optarg);
			break;
		case 'm':
			max_mem = 1ull * atoi_positive("Memory size", optarg) * SZ_1G;
			break;
		case 'H':
			hugepages = true;
			break;
		case 'h':
		default:
			printf("usage: %s [-c nr_vcpus] [-m max_mem_in_gb] [-H]\n",
			       argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	if (!max_mem)
		max_mem = get_default_max_mem(hugepages);

	TEST_ASSERT((max_mem % PG_SIZE_2M) == 0,
		    "Memory size must be 2 MiB aligned, got 0x%lx", max_mem);

	vcpus = malloc(nr_test_vcpus * sizeof(*vcpus));
	threads = malloc(nr_test_vcpus * sizeof(*threads));
	thread_args = malloc(nr_test_vcpus * sizeof(*thread_args));
	ap_can_run = calloc(nr_test_vcpus, sizeof(*ap_can_run));
	TEST_ASSERT(vcpus && threads && thread_args && ap_can_run,
		    "Failed to allocate host-side vCPU state");

	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, nr_test_vcpus, 0);
	backing_src = hugepages ? VM_MEM_SRC_SHARED_HUGETLB :
				 VM_MEM_SRC_ANONYMOUS;

	vm_userspace_mem_region_add(vm, backing_src,
				    MEM_REGION_GPA, MEM_REGION_SLOT,
				    max_mem / vm->page_size, 0);
	mem = addr_gpa2hva(vm, MEM_REGION_GPA);
	pr_info("Preparing %lu MiB memslot (%s backing)\n",
		(unsigned long)(max_mem >> 20),
		hugepages ? "hugetlb" : "anonymous");
	/* Allocate vCPU stacks from the fixed large region instead of slot0. */
	vm->memslots[MEM_REGION_DATA] = MEM_REGION_SLOT;
	pr_info("Building guest mappings...\n");
	virt_map_level(vm, MEM_REGION_GPA, MEM_REGION_GPA,
		       max_mem, PG_LEVEL_2M);

	pr_info("Pre-faulting host backing before vCPU spawn...\n");
	for (offset = 0; offset < max_mem; offset += vm->page_size) {
		((uint8_t *)mem)[offset] = 0xaa;
		if (offset && !(offset % SZ_1G))
			pr_info("prefaulted %lu MiB\n",
				(unsigned long)(offset >> 20));
	}

	guest_nr_vcpus = nr_test_vcpus;
	sync_global_to_guest(vm, guest_nr_vcpus);
	pages_per_vcpu = (max_mem / vm->page_size) / nr_test_vcpus;
	TEST_ASSERT(pages_per_vcpu > 0, "Too many vCPUs for %lu MiB memslot",
		    (unsigned long)(max_mem >> 20));

	pr_info("Spawning %u protected vCPUs over a %lu MiB memslot\n",
		nr_test_vcpus, (unsigned long)(max_mem >> 20));

	for (vcpu_id = 0; vcpu_id < nr_test_vcpus; vcpu_id++) {
		vcpus[vcpu_id] = vm_vcpu_add(vm, vcpu_id,
					   vcpu_id ? guest_code_ap : guest_code_bsp);
		start_gpa = MEM_REGION_GPA + vcpu_id * pages_per_vcpu * vm->page_size;
		end_gpa = vcpu_id == nr_test_vcpus - 1 ? MEM_REGION_GPA + max_mem :
			start_gpa + pages_per_vcpu * vm->page_size;
		vcpu_args_set(vcpus[vcpu_id], 3, start_gpa, end_gpa,
			      vm->page_size);
		thread_args[vcpu_id].vcpu = vcpus[vcpu_id];
		thread_args[vcpu_id].vcpu_id = vcpu_id;
	}

	atomic_set(&rendezvous, nr_test_vcpus + 1);
	clock_gettime(CLOCK_MONOTONIC, &spawn_start);

	for (vcpu_id = 0; vcpu_id < nr_test_vcpus; vcpu_id++)
		check_create_thread(&threads[vcpu_id], run_vcpu,
				    &thread_args[vcpu_id]);

	rendezvous_with_vcpus(&spawn_done, "spawning");
	spawn_elapsed = timespec_sub(spawn_done, spawn_start);
	pr_info("spawn = %ld.%.9lds\n",
		spawn_elapsed.tv_sec, spawn_elapsed.tv_nsec);

	if (nr_test_vcpus > 1 && ap_bringup_timed) {
		ap_bringup_elapsed = timespec_sub(ap_bringup_done, spawn_start);
		pr_info("ap_bringup = %ld.%.9lds\n",
			ap_bringup_elapsed.tv_sec, ap_bringup_elapsed.tv_nsec);
	}

	for (vcpu_id = 0; vcpu_id < nr_test_vcpus; vcpu_id++)
		pthread_join(threads[vcpu_id], NULL);

	kvm_vm_free(vm);
	free(ap_can_run);
	free(thread_args);
	free(threads);
	free(vcpus);
	return 0;
}