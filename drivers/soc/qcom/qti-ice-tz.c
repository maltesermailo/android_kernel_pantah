// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto TZ library for storage encryption.
 *
 * Copyright (c) 2021, Linux Foundation. All rights reserved.
 */

#include <asm/cacheflush.h>
#include <linux/qcom_scm.h>
#include <linux/module.h>
#include <linux/qti-ice-common.h>
#include "qti-ice-platform.h"

#define ICE_CIPHER_MODE_XTS_256 3
#define AES_256_XTS_WRAPPED_KEY_SIZE 128
#define UFS_CE 10
#define SDCC_CE 20
#define UFS_CARD_CE 30

int qti_ice_program_key(struct qti_ice_entry *ice_entry,
			   const struct blk_crypto_key *key, unsigned int slot,
			   unsigned int data_unit_mask, int capid)
{
	int err = 0;
	int i = 0;
	union {
		u8 bytes[AES_256_XTS_WRAPPED_KEY_SIZE];
		u32 words[AES_256_XTS_WRAPPED_KEY_SIZE / sizeof(u32)];
	} key_new;

	memcpy(key_new.bytes, key->raw, key->size);
	if (!key->crypto_cfg.is_hw_wrapped) {
	/*
	 * The SCM call byte-swaps the 32-bit words of the key.  So we have to
	 * do the same, in order for the final key be correct.
	 */
		for (i = 0; i < ARRAY_SIZE(key_new.words); i++)
			__cpu_to_be32s(&key_new.words[i]);
	}

	err = qcom_scm_ice_set_key(slot, key_new.bytes, key->size,
					ICE_CIPHER_MODE_XTS_256,
					data_unit_mask);
	if (err)
		pr_err("%s:SCM call Error: 0x%x slot %d\n",
				__func__, err, slot);

	memzero_explicit(&key_new, sizeof(key_new));
	return err;
}
EXPORT_SYMBOL(qti_ice_program_key);

int qti_ice_invalidate_key(struct qti_ice_entry *ice_entry,
			      unsigned int slot)
{
	int err = 0;

	err = qcom_scm_ice_invalidate_key(slot);
	if (err)
		pr_err("%s:SCM call Error: 0x%x\n", __func__, err);

	return err;
}
EXPORT_SYMBOL(qti_ice_invalidate_key);

int qti_ice_derive_raw_secret_platform(
				struct qti_ice_entry *ice_entry,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	//TODO: SCM interface call for TZ based wrapped keys
	return 0;
}
EXPORT_SYMBOL(qti_ice_derive_raw_secret_platform);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ICE TZ library for storage encryption");
