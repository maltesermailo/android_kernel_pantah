// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#include <crypto/sha256_base.h>
#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>

const struct pkvm_module_ops *pkvm_ops;

#define puts(s)                            \
	do {                               \
		if (pkvm_ops != NULL)      \
			pkvm_ops->puts(s); \
	} while (false)

#define puts_debug(s) puts("[pkvm-dtrng][DEBUG]" s)
#define puts_error(s) puts("[pkvm-dtrng][ERROR]" s);

static bool handle_guest_hvc(struct arm_smccc_1_2_regs *r,
			     struct arm_smccc_res *res)
{
	switch (r->a0) {
	case ARM_SMCCC_TRNG_VERSION:
		puts_debug("handle_guest_hvc ARM_SMCCC_TRNG_VERSION");
		res->a0 = 1 << 16 | 0; // Version 1.0
		return true;
	case ARM_SMCCC_TRNG_FEATURES:
		switch (r->a1) {
		case ARM_SMCCC_TRNG_VERSION:
		case ARM_SMCCC_TRNG_FEATURES:
		/*TODO: case ARM_SMCCC_TRNG_GET_UUID:*/
		case ARM_SMCCC_TRNG_RND32:
		case ARM_SMCCC_TRNG_RND64:
			res->a0 = SMCCC_RET_SUCCESS;
		}
		return true;
	case ARM_SMCCC_TRNG_RND32:
		fallthrough;
	case ARM_SMCCC_TRNG_RND64:
		puts_debug("handle_guest_hvc ARM_SMCCC_TRNG_RND64");
		memset(res, 0x42, sizeof(*res));
		res->a0 = SMCCC_RET_SUCCESS;
		return true;
	default:
		return false;
	}
}

int pkvm_dtrng_init(const struct pkvm_module_ops *ops)
{
	int ret;
	u64 digest[SHA256_DIGEST_SIZE / sizeof(u64)];
	static const char test_data[] = "abcd";
	size_t i;

	pkvm_ops = ops;

	puts_debug("pKVM DTRNG driver loaded");

	puts_debug("== SHA start ==");
	sha256(test_data, sizeof(test_data), (u8 *)digest);
	for (i = 0; i < ARRAY_SIZE(digest); i++) {
		pkvm_ops->putx64(digest[i]);
	}
	puts_debug("== SHA stop ==");

	ret = ops->register_guest_hvc_handler(handle_guest_hvc);
	if (ret) {
		puts_error("register_host_smc_handler failed returned:");
		pkvm_ops->putx64(ret);
		return ret;
	}

	return 0;
}

#undef memcpy
#undef memset

void *memcpy(void *to, const void *from, size_t count)
{
	return pkvm_ops->memcpy(to, from, count);
}

void *memset(void *dst, int c, size_t count)
{
	return pkvm_ops->memset(dst, c, count);
}
