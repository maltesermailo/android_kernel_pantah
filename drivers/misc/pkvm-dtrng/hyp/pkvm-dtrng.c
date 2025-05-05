// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#include <crypto/sha256_base.h>
#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>
#include <nvhe/spinlock.h>

#include "drbg.h"
#include "dtrng-common.h"

struct drbg_state {
	hyp_spinlock_t lock;

	u8 digest[SHA256_DIGEST_SIZE];
	size_t counter;
};

static struct drbg_state drbg_states[CONFIG_NR_CPUS];

static inline struct drbg_state *get_drbg_state(void)
{
	return &drbg_states[_hyp_smp_processor_id()];
}

static int drbg_state_update_locked(struct drbg_state *state,
				    const char *entropy_input,
				    size_t entropy_size)
{
	(void)entropy_input;
	(void)entropy_size;

	static const char tmp_hmac_key[] = "HMAC HMAC HMAC";
	struct hmac_sha256 hmac;
	int cpu = _hyp_smp_processor_id();
	unsigned long clk = read_sysreg(cntvct_el0);

	/* TODO: Actual DRBG implementation */
	hmac_sha256_init(&hmac, tmp_hmac_key, sizeof(tmp_hmac_key));

	/* Mixin CPU ID and Virtual Timer value just for the lolz */
	hmac_sha256_update(&hmac, (void *)&cpu, sizeof(cpu));
	hmac_sha256_update(&hmac, (void *)&clk, sizeof(clk));

	hmac_sha256_update(&hmac, state->digest, sizeof(state->digest));

	if (entropy_size > 0)
		hmac_sha256_update(&hmac, entropy_input, entropy_size);

	hmac_sha256_finish(&hmac, state->digest);

	state->counter = 0;

	return 0;
}

static int feed(void *addr, size_t size)
{
	int ret;
	size_t i;
	struct drbg_state *state;

	/* TODO: Actual DRBG reseeding */

	for (i = 0; i < ARRAY_SIZE(drbg_states); i++) {
		state = &drbg_states[i];

		hyp_spin_lock(&state->lock);

		ret = drbg_state_update_locked(state, addr, size);
		if (ret) {
			mod_error("drbg_state_update_locked failed returned:");
			hyp_putx64(ret);

			hyp_spin_unlock(&state->lock);
			return ret;
		}

		hyp_spin_unlock(&state->lock);
	}
	return 0;
}

void pkvm_dtrng_feed_hvc(struct user_pt_regs *regs)
{
	int ret = -EINVAL;
	phys_addr_t addr = regs->regs[1];
	size_t size = regs->regs[2];
	void *hyp_addr;

	if (!mod_ops)
		goto exit;

	if (size > PAGE_SIZE) {
		mod_error("size too big");
		goto exit;
	}

	hyp_addr = hyp_fixmap_map(addr);
	if (!hyp_addr) {
		mod_error("fixmap_map failed");
		goto exit;
	}

	ret = feed(hyp_addr, size);

	hyp_fixmap_unmap();
exit:
	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ret;
}

static int drbg_random_u8_locked(struct drbg_state *state, u8 *out)
{
	int ret;

	if (state->counter >= sizeof(state->digest)) {
		ret = drbg_state_update_locked(state, NULL, 0);
		if (ret)
			return ret;
	}

	*out = state->digest[state->counter];

	state->counter++;

	return 0;
}

static int drbg_random_ul_locked(struct drbg_state *state, unsigned long *out)
{
	union {
		unsigned long ul;
		u8 bytes[sizeof(unsigned long)];
	} val;
	int ret;
	size_t i;

	for (i = 0; i < sizeof(val); i++) {
		ret = drbg_random_u8_locked(state, &val.bytes[i]);
		if (ret)
			return ret;
	}

	*out = val.ul;

	return 0;
}

static bool handle_guest_hvc(struct arm_smccc_1_2_regs *r,
			     struct arm_smccc_res *res)
{
	struct drbg_state *state = get_drbg_state();

	switch (r->a0) {
	case ARM_SMCCC_TRNG_VERSION:
		mod_debug("handle_guest_hvc ARM_SMCCC_TRNG_VERSION");
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
		res->a0 = SMCCC_RET_SUCCESS;
		hyp_spin_lock(&state->lock);
		drbg_random_ul_locked(state, &res->a1);
		drbg_random_ul_locked(state, &res->a2);
		drbg_random_ul_locked(state, &res->a3);
		hyp_spin_unlock(&state->lock);
		return true;
	default:
		return false;
	}
}

const struct pkvm_module_ops *mod_ops;

int pkvm_dtrng_init(const struct pkvm_module_ops *ops)
{
	int ret;
	size_t i;
	struct drbg_state *state;

	mod_ops = ops;

	mod_debug("pKVM DTRNG driver loaded");

	for (i = 0; i < ARRAY_SIZE(drbg_states); i++) {
		state = &drbg_states[i];
		hyp_spin_lock_init(&state->lock);
	}

	ret = __pkvm_register_guest_hvc_handler(handle_guest_hvc);
	if (ret) {
		mod_error("register_host_smc_handler failed returned:");
		hyp_putx64(ret);
		return ret;
	}

	return 0;
}

void *memcpy(void *to, const void *from, size_t count)
{
	return CALL_FROM_OPS(memcpy, to, from, count);
}

void *memset(void *dst, int c, size_t count)
{
	return CALL_FROM_OPS(memset, dst, c, count);
}
