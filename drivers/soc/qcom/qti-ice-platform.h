/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QTI_ICE_PLATFORM_H
#define _QTI_ICE_PLATFORM_H

#include <linux/blk-crypto.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/device.h>

#if IS_ENABLED(CONFIG_QTI_ICE_COMMON)
int qti_ice_program_key(struct qti_ice_entry *ice_entry,
			   const struct blk_crypto_key *key,
			   unsigned int slot,
			   unsigned int data_unit_mask, int capid);
int qti_ice_invalidate_key(struct qti_ice_entry *ice_entry,
			      unsigned int slot);
int qti_ice_derive_raw_secret_platform(
				struct qti_ice_entry *ice_entry,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size);

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
void qti_ice_disable_platform(struct qti_ice_entry *ice_entry);
#else
static inline void qti_ice_disable_platform(
				struct qti_ice_entry *ice_entry)
{}
#endif /* CONFIG_QTI_HW_KEY_MANAGER */
#else
static inline int qti_ice_program_key(
				struct qti_ice_entry *ice_entry,
				const struct blk_crypto_key *key,
				unsigned int slot,
				unsigned int data_unit_mask, int capid)
{
	return -EOPNOTSUPP;
}
static inline int qti_ice_invalidate_key(
		struct qti_ice_entry *ice_entry, unsigned int slot)
{
	return -EOPNOTSUPP;
}
static inline int qti_ice_derive_raw_secret_platform(
				struct qti_ice_entry *ice_entry,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	return -EOPNOTSUPP;
}

static inline void qti_ice_disable_platform(
				struct qti_ice_entry *ice_entry)
{}
#endif /* CONFIG_QTI_ICE_COMMON */
#endif /* _QTI_ICE_PLATFORM_H */
