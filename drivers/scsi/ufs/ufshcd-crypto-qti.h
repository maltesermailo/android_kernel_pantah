/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _UFSHCD_CRYPTO_QTI_H
#define _UFSHCD_CRYPTO_QTI_H

#include "ufshcd.h"
#include "ufshcd-crypto.h"
#include "ufs-qcom.h"

#ifdef CONFIG_SCSI_UFS_CRYPTO
int ufshcd_crypto_qti_ice_init(struct ufs_qcom_host *host);
int ufshcd_crypto_qti_ice_enable(struct ufs_qcom_host *host);
int ufshcd_crypto_qti_ice_resume(struct ufs_qcom_host *host);
int ufshcd_crypto_qti_ice_debug(struct ufs_qcom_host *host);
void ufshcd_crypto_qti_ice_disable(struct ufs_qcom_host *host);
int ufshcd_crypto_ice_program_key(struct ufs_hba *hba,
				 const union ufs_crypto_cfg_entry *cfg,
			     const struct blk_crypto_key *key, int slot);
#else
static inline int ufshcd_crypto_qti_ice_init(struct ufs_qcom_host *host)
{
	return 0;
}
static inline int ufshcd_crypto_qti_ice_enable(struct ufs_qcom_host *host)
{
	return 0;
}
static inline int ufs_qcom_ice_resume(struct ufs_qcom_host *host)
{
	return 0;
}
static inline int ufshcd_crypto_qti_ice_debug(struct ufs_qcom_host *host)
{
	return 0;
}
static inline void ufshcd_crypto_qti_ice_disable(struct ufs_qcom_host *host)
{
	return;
}
#define ufs_qcom_ice_program_key NULL
#endif /* CONFIG_SCSI_UFS_CRYPTO */
#endif /* _UFSHCD_CRYPTO_QTI_H */