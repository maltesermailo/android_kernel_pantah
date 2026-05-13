/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_ARM64_TLBBATCH_H
#define _ARCH_ARM64_TLBBATCH_H

<<<<<<< HEAD   (dd726c1ecdb56955b4e3184420e896e32d0a5b26 Merge android17-6.18 into android17-6.18-lts)
#include <linux/android_kabi.h>
||||||| BASE   (ee5ce483d42809b6c9e5bb25c33601e54229128f arm64: cputype: Add C1-Pro definitions)
=======
#include <linux/cpumask.h>
>>>>>>> BRANCH (c6c87a23de4bdf5a8a8a26d9d269f4026e35afef arm64: errata: Work around early CME DVMSync acknowledgement)

struct arch_tlbflush_unmap_batch {
#ifdef CONFIG_ARM64_ERRATUM_4193714
	/*
	 * Track CPUs that need SME DVMSync on completion of this batch.
	 * Otherwise, the arm64 HW can do tlb shootdown, so we don't need to
	 * record cpumask for sending IPI
	 */
<<<<<<< HEAD   (dd726c1ecdb56955b4e3184420e896e32d0a5b26 Merge android17-6.18 into android17-6.18-lts)
	ANDROID_KABI_RESERVE(1);
||||||| BASE   (ee5ce483d42809b6c9e5bb25c33601e54229128f arm64: cputype: Add C1-Pro definitions)
=======
	cpumask_var_t cpumask;
#endif
>>>>>>> BRANCH (c6c87a23de4bdf5a8a8a26d9d269f4026e35afef arm64: errata: Work around early CME DVMSync acknowledgement)
};

#endif /* _ARCH_ARM64_TLBBATCH_H */
