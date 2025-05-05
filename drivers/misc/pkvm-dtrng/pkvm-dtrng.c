// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

int kvm_nvhe_sym(pkvm_dtrng_init)(const struct pkvm_module_ops *ops);

static int __init dtrng_init(void)
{
	int ret;
	unsigned long pkvm_module_token;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(pkvm_dtrng_init),
				   &pkvm_module_token);
	if (ret) {
		pr_err("Failed to register pKVM module: %d\n", ret);
		return ret;
	}

	return 0;
}

module_init(dtrng_init);

MODULE_AUTHOR("Bartlomiej Grzesik <bgrzesik@google.com>");
MODULE_DESCRIPTION("DTRNG");
MODULE_LICENSE("GPL v2");
