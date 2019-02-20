
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

static int cf_cpufreq_target_index(struct cpufreq_policy *policy, unsigned int index) {
	return 0;
}

static int cf_cpufreq_driver_init(struct cpufreq_policy *policy) {
	struct cpufreq_frequency_table *freq_table;

	freq_table = kcalloc(1, sizeof(*freq_table), GFP_ATOMIC);
	if (!freq_table)
		return -ENOMEM;
	freq_table[0].frequency = 1;
	freq_table[1].frequency = CPUFREQ_TABLE_END;

	return cpufreq_table_validate_and_show(policy, freq_table);
}

static int cf_cpufreq_verify(struct cpufreq_policy *policy) {
	return 0;
}

static struct cpufreq_driver cf_cpufreq_driver = {
	.name = "cf",
	.target_index = cf_cpufreq_target_index,
	.init = cf_cpufreq_driver_init,
	.verify = cf_cpufreq_verify,
};

static int __init cf_cpufreq_init(void) {
	return cpufreq_register_driver(&cf_cpufreq_driver);
}
