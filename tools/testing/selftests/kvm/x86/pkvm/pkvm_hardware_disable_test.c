// SPDX-License-Identifier: GPL-2.0-only
/*
 * This test is intended to reproduce a crash that happens when
 * kvm_arch_hardware_disable is called and it attempts to unregister the user
 * return notifiers.
 */
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "asm/kvm.h"
#include <linux/kvm_para.h>
#include <test_util.h>

#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_boot.h"
#include "pkvm/pkvm_util.h"
#include "ucall_common.h"

#define VCPU_NUM 4
#define SLEEPING_THREAD_NUM (1 << 4)
#define FORK_NUM (1ULL << 9)
#define DELAY_US_MAX 2000

sem_t *sem;
static pthread_mutex_t ap_start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ap_start_cond = PTHREAD_COND_INITIALIZER;
static bool ap_can_run[VCPU_NUM];

enum {
	UCALL_AP_STARTED = 1,
	UCALL_AP_START_FAIL,
};

struct vcpu_thread_args {
	struct kvm_vcpu *vcpu;
	uint32_t vcpu_id;
};

static void guest_code_ap(void)
{
	for (;;)
		;  /* Some busy work */
}

static void guest_code_bsp(void)
{
	uint32_t i;

	/* Bring up all APs before busy-looping; required in pKVM protected VMs */
	for (i = 1; i < VCPU_NUM; i++) {
		long ret = kvm_hypercall(PKVM_GHC_START_CPU, i,
					 PKVM_AP_SIPI_BOOT_GPA, 0, 0);

		if (ret) {
			GUEST_SYNC_ARGS(UCALL_AP_START_FAIL, i, ret, 0, 0);
			GUEST_DONE();
		}

		GUEST_SYNC_ARGS(UCALL_AP_STARTED, i, 0, 0, 0);
	}

	for (;;)
		;  /* Some busy work */
}

static void *run_vcpu(void *arg)
{
	struct vcpu_thread_args *thread_args = arg;
	struct kvm_vcpu *vcpu = thread_args->vcpu;
	struct kvm_run *run = vcpu->run;
	struct ucall uc;
	uint32_t ap_id;

	if (thread_args->vcpu_id) {
		pthread_mutex_lock(&ap_start_lock);
		while (!ap_can_run[thread_args->vcpu_id])
			pthread_cond_wait(&ap_start_cond, &ap_start_lock);
		pthread_mutex_unlock(&ap_start_lock);
		pr_debug("%s: AP vCPU %u released for run\n",
			 __func__, thread_args->vcpu_id);
	}

	for (;;) {
		vcpu_run(vcpu);

		if (thread_args->vcpu_id)
			break;

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			if (uc.args[1] == UCALL_AP_START_FAIL)
				TEST_FAIL("PKVM_GHC_START_CPU failed for vCPU %lu, rc=%lu",
					  uc.args[2], uc.args[3]);

			TEST_ASSERT(uc.args[1] == UCALL_AP_STARTED,
				    "%s: unexpected BSP sync stage %lu",
				    __func__, uc.args[1]);

			ap_id = uc.args[2];
			TEST_ASSERT(ap_id > 0 && ap_id < VCPU_NUM,
				    "%s: invalid AP id %u", __func__, ap_id);
			pr_debug("%s: BSP reported AP vCPU %u started\n",
				 __func__, ap_id);

			pthread_mutex_lock(&ap_start_lock);
			ap_can_run[ap_id] = true;
			pthread_cond_broadcast(&ap_start_cond);
			pthread_mutex_unlock(&ap_start_lock);
			continue;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			break;
		}

		break;
	}

	TEST_ASSERT(false, "%s: exited with reason %d: %s",
		    __func__, run->exit_reason,
		    exit_reason_str(run->exit_reason));
	pthread_exit(NULL);
}

static void *sleeping_thread(void *arg)
{
	int fd;

	while (true) {
		fd = open("/dev/null", O_RDWR);
		close(fd);
	}
	TEST_ASSERT(false, "%s: exited", __func__);
	pthread_exit(NULL);
}

static inline void check_create_thread(pthread_t *thread, pthread_attr_t *attr,
				       void *(*f)(void *), void *arg)
{
	int r;

	r = pthread_create(thread, attr, f, arg);
	TEST_ASSERT(r == 0, "%s: failed to create thread", __func__);
}

static inline void check_set_affinity(pthread_t thread, cpu_set_t *cpu_set)
{
	int r;

	r = pthread_setaffinity_np(thread, sizeof(cpu_set_t), cpu_set);
	TEST_ASSERT(r == 0, "%s: failed set affinity", __func__);
}

static inline void check_join(pthread_t thread, void **retval)
{
	int r;

	r = pthread_join(thread, retval);
	TEST_ASSERT(r == 0, "%s: failed to join thread", __func__);
}

static void run_test(uint32_t run)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	cpu_set_t cpu_set;
	pthread_t threads[VCPU_NUM];
	struct vcpu_thread_args thread_args[VCPU_NUM];
	pthread_t throw_away;
	void *b;
	uint32_t i, j;

	CPU_ZERO(&cpu_set);
	for (i = 0; i < VCPU_NUM; i++)
		CPU_SET(i, &cpu_set);

	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, VCPU_NUM, 0);

	pr_debug("%s: [%d] start vcpus\n", __func__, run);
	for (i = 0; i < VCPU_NUM; ++i) {
		vcpu = vm_vcpu_add(vm, i, i == 0 ? guest_code_bsp : guest_code_ap);
		thread_args[i].vcpu = vcpu;
		thread_args[i].vcpu_id = i;
	}

	for (i = 0; i < VCPU_NUM; ++i) {
		check_create_thread(&threads[i], NULL, run_vcpu, &thread_args[i]);
		check_set_affinity(threads[i], &cpu_set);

		for (j = 0; j < SLEEPING_THREAD_NUM; ++j) {
			check_create_thread(&throw_away, NULL, sleeping_thread,
					    (void *)NULL);
			check_set_affinity(throw_away, &cpu_set);
		}
	}
	pr_debug("%s: [%d] all threads launched\n", __func__, run);
	sem_post(sem);
	for (i = 0; i < VCPU_NUM; ++i)
		check_join(threads[i], &b);
	/* Should not be reached */
	TEST_ASSERT(false, "%s: [%d] child escaped the ninja", __func__, run);
}

void wait_for_child_setup(pid_t pid)
{
	/*
	 * Wait for the child to post to the semaphore, but wake up periodically
	 * to check if the child exited prematurely.
	 */
	for (;;) {
		struct timespec wait_period;
		int status;

		clock_gettime(CLOCK_REALTIME, &wait_period);
		wait_period.tv_sec += 1;

		if (!sem_timedwait(sem, &wait_period))
			return;

		/* Child is still running, keep waiting. */
		if (pid != waitpid(pid, &status, WNOHANG))
			continue;

		/*
		 * Child is no longer running, which is not expected.
		 *
		 * If it exited with a non-zero status, we explicitly forward
		 * the child's status in case it exited with KSFT_SKIP.
		 */
		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
		else
			TEST_ASSERT(false, "Child exited unexpectedly");
	}
}

int main(int argc, char **argv)
{
	uint32_t i;
	int s, r;
	pid_t pid;

	TEST_REQUIRE(is_pkvm_enabled());

	sem = sem_open("vm_sem", O_CREAT | O_EXCL, 0644, 0);
	sem_unlink("vm_sem");

	for (i = 0; i < FORK_NUM; ++i) {
		pid = fork();
		TEST_ASSERT(pid >= 0, "%s: unable to fork", __func__);
		if (pid == 0)
			run_test(i); /* This function always exits */

		pr_debug("%s: [%d] waiting semaphore\n", __func__, i);
		wait_for_child_setup(pid);
		r = (rand() % DELAY_US_MAX) + 1;
		pr_debug("%s: [%d] waiting %dus\n", __func__, i, r);
		usleep(r);
		r = waitpid(pid, &s, WNOHANG);
		TEST_ASSERT(r != pid,
			    "%s: [%d] child exited unexpectedly status: [%d]",
			    __func__, i, s);
		pr_debug("%s: [%d] killing child\n", __func__, i);
		kill(pid, SIGKILL);
	}

	sem_close(sem);
	exit(0);
}