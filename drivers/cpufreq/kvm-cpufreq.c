// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Google LLC
 */

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/arm-smccc.h>

static int kvm_cpufreq_get_freqtbl_num_entries(void)
{
	struct arm_smccc_res hvc_res;
	u32 freq = 1UL;
	int idx = 0;

	preempt_disable();
	while (freq != CPUFREQ_ENTRY_INVALID && freq != CPUFREQ_TABLE_END) {
		arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_TBL_FUNC_ID, idx, &hvc_res);
		freq = hvc_res.a1;
		idx++;
		if (hvc_res.a0) {
			idx = -ENODEV;
			goto err;
		}
	}

err:
	preempt_enable();
	return idx;
}

static int kvm_cpufreq_populate_freqtbl(struct cpufreq_frequency_table	*table)
{
	struct arm_smccc_res hvc_res;
	struct cpufreq_frequency_table	*pos;
	int idx, ret = 0;

	preempt_disable();
	cpufreq_for_each_valid_entry_idx(pos, table, idx) {
		arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_TBL_FUNC_ID, idx, &hvc_res);
		pos->frequency = hvc_res.a1;
		if (hvc_res.a0) {
			ret = -ENODEV;
			goto err;
		}
	}

err:
	preempt_enable();
	return ret;
}

static int kvm_cpufreq_target_index(struct cpufreq_policy *policy,
		unsigned int index) {
	return 0;
}
static unsigned int kvm_cpufreq_fast_switch(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
	struct arm_smccc_res hvc_res;
	u32 util = sched_cpu_util_freq(policy->cpu);

	preempt_disable();
	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_UCLAMP_FUNC_ID,
			     util, 1024, &hvc_res);
	preempt_enable();
	return target_freq;
}

static const struct of_device_id kvm_cpufreq_match[] = {
	{ .compatible = "kvm,cpufreq"},
	{}
};
MODULE_DEVICE_TABLE(of, kvm_cpufreq_match);

static int kvm_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct device *cpu_dev;
	struct cpufreq_frequency_table	*table;
	int num_entries;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("%s: failed to get cpu%d device\n", __func__,
		       policy->cpu);
		return -ENODEV;
	}

	num_entries = kvm_cpufreq_get_freqtbl_num_entries();
	if (num_entries == -ENODEV)
		return -ENODEV;

	table = kcalloc(num_entries, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table[num_entries-1].frequency = CPUFREQ_TABLE_END;

	if (kvm_cpufreq_populate_freqtbl(table))
		return -ENODEV;

	policy->freq_table = table;
	policy->dvfs_possible_from_any_cpu = false;
	policy->fast_switch_possible = true;

	return 0;
}

static int kvm_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	return 0;
}

static struct cpufreq_driver cpufreq_kvm_driver = {
	.name		= "kvm-cpufreq",
	.init		= kvm_cpufreq_cpu_init,
	.exit		= kvm_cpufreq_cpu_exit,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= kvm_cpufreq_target_index,
	.fast_switch	= kvm_cpufreq_fast_switch,
	.attr		= cpufreq_generic_attr,
};

static int kvm_cpufreq_driver_probe(struct platform_device *pdev)
{
	int ret;

	ret = cpufreq_register_driver(&cpufreq_kvm_driver);
	if (ret) {
		dev_err(&pdev->dev, "KVM CPUFreq driver failed to register: %d\n", ret);
		return ret;
	} else {
		dev_err(&pdev->dev, "KVM CPUFreq driver initialized\n");
	}

	return 0;
}

static int kvm_cpufreq_driver_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&cpufreq_kvm_driver);
	return 0;
}

static struct platform_driver kvm_cpufreq_driver = {
	.probe = kvm_cpufreq_driver_probe,
	.remove = kvm_cpufreq_driver_remove,
	.driver = {
		.name = "kvm-cpufreq",
		.of_match_table = kvm_cpufreq_match,
	},
};

module_platform_driver(kvm_cpufreq_driver);
MODULE_DESCRIPTION("KVM cpufreq driver");
MODULE_LICENSE("GPL");
