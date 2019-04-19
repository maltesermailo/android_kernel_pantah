// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 LK Trusty Authors.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/trusty/trusty_custom_smc.h>

#define IKGT_SMC_HC_ID 0x74727500

/*
 * IRQ numbers to be reversed at core init stage for Trusty.
 * Linux kernel has reversed following IRQs:
 * 	IRQ 0 -- legacy timer;
 * 	IRQ 2 -- cascade interrupt to send interrupt controller.
 *
 * Trusty IRQs to be reserved:
 *  IRQ 1 -- Trusty timer interrupt
 */
#define IRQ1	0x1

#define EXPECTED_VECTOR(x) (FIRST_EXTERNAL_VECTOR + x)

static irqreturn_t stub_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}

struct irqaction irq1 = {
	.handler = stub_action,
	.flags = IRQF_NO_THREAD,
	.name = "trusty"
};

static inline ulong smc_instr(ulong r0, ulong r1, ulong r2, ulong r3,
			      struct trusty_custom_smc *dummy_smc)
{
	register unsigned long smc_id asm("rax") = IKGT_SMC_HC_ID;

	__asm__ __volatile__ (
			"vmcall"
			: "=D" (r0)
			: "r" (smc_id),  "D" (r0), "S" (r1), "d" (r2), "b" (r3)
	);

	return r0;
}

struct trusty_custom_smc x86_64_smc = {
	.smc = smc_instr,
};

static const struct of_device_id trusty_x86_64_of_match[] = {
	{ .compatible = "android,trusty-x86_64-smc-v1"},
	{},
};

static int trusty_x86_64_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;

	dev_dbg(&pdev->dev, "Initializing trusty x86_64 driver\n");

	if (!node) {
		dev_err(&pdev->dev, "of_node required\n");
		return -EINVAL;
	}

	trusty_custom_smc_set_drvdata(&pdev->dev, &x86_64_smc);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add children: %d\n", ret);
		x86_64_smc.smc = NULL;
		trusty_custom_smc_set_drvdata(&pdev->dev, &x86_64_smc);
		return ret;
	}

	return 0;
}

static int trusty_x86_64_remove_child(struct device *dev, void *data)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int trusty_x86_64_remove(struct platform_device *pdev)
{
	device_for_each_child(&pdev->dev, NULL, trusty_x86_64_remove_child);

	return 0;
}

static struct platform_driver trusty_x86_64_driver = {
	.probe = trusty_x86_64_probe,
	.remove = trusty_x86_64_remove,
	.driver	= {
		.name = "trusty_x86_64",
		.owner = THIS_MODULE,
		.of_match_table = trusty_x86_64_of_match,
	},
};

static int __init trusty_x86_64_driver_init(void)
{
	return platform_driver_register(&trusty_x86_64_driver);
}

static void __exit trusty_x86_64_driver_exit(void)
{
	platform_driver_unregister(&trusty_x86_64_driver);
}

static int __init trusty_x86_64_irq_init(void)
{
	struct irq_cfg *cfg;

	setup_irq(IRQ1, &irq1);

	cfg = irq_cfg(IRQ1);
	BUG_ON(!cfg);
	BUG_ON(cfg->vector != EXPECTED_VECTOR(IRQ1));

	return 0;
}

core_initcall(trusty_x86_64_irq_init);

subsys_initcall(trusty_x86_64_driver_init);
module_exit(trusty_x86_64_driver_exit);
