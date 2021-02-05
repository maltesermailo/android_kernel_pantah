// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto HWKM library for storage encryption.
 *
 * Copyright (c) 2021, Linux Foundation. All rights reserved.
 */

#include <linux/qti-ice-common.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "qti-ice-platform.h"

union ice_cfg {
	__le32 regval[2];
	struct {
		u8 dusize;
		u8 capidx;
		u8 nop;
		u8 cfge;
		u8 dumb[4];
	};
};

int qti_ice_program_key(struct qti_ice_entry *ice_entry,
			   const struct blk_crypto_key *key, unsigned int slot,
			   unsigned int data_unit_mask, int capid)
{
	//TODO: HWKM based wrapped keys
	return 0;
}
EXPORT_SYMBOL(qti_ice_program_key);

int qti_ice_invalidate_key(struct qti_ice_entry *ice_entry,
			      unsigned int slot)
{
	//TODO: HWKM based wrapped keys
	return 0;
}
EXPORT_SYMBOL(qti_ice_invalidate_key);

void qti_ice_disable_platform(struct qti_ice_entry *ice_entry)
{
	return 0;
}
EXPORT_SYMBOL(qti_ice_disable_platform);

int qti_ice_derive_raw_secret_platform(
				struct qti_ice_entry *ice_entry,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	//TODO: HWKM based wrapped keys
	return 0;
}
EXPORT_SYMBOL(qti_ice_derive_raw_secret_platform);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ICE HWKM library for storage encryption");
