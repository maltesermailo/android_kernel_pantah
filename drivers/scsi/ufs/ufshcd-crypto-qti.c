// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS Crypto ops QTI implementation.
 *
 * Copyright (c) 2021, Linux Foundation. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/qti-ice-common.h>

#include "ufshcd-crypto-qti.h"

/* Blk-crypto modes supported by UFS crypto */
static const struct ufs_crypto_alg_entry {
	enum ufs_crypto_alg ufs_alg;
	enum ufs_crypto_key_size ufs_key_size;
} ufs_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.ufs_alg = UFS_CRYPTO_ALG_AES_XTS,
		.ufs_key_size = UFS_CRYPTO_KEY_SIZE_256,
	},
};

int ufshcd_crypto_qti_ice_init(struct ufs_qcom_host *host)
{
	struct ufs_hba *hba = host->hba;
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	void __iomem *ice_mmio;
	int err;

	if (!(ufshcd_readl(hba, REG_CONTROLLER_CAPABILITIES) &
	      MASK_CRYPTO_SUPPORT))
		return 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ice");
	if (!res) {
		dev_warn(dev, "ICE registers not found\n");
		goto disable;
	}

	if (!qcom_scm_ice_available()) {
		dev_warn(dev, "ICE SCM interface not found\n");
		goto disable;
	}

	ice_mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(ice_mmio)) {
		err = PTR_ERR(ice_mmio);
		dev_err(dev, "Failed to map ICE registers; err=%d\n", err);
		hba->caps &= ~UFSHCD_CAP_CRYPTO;
		return err;
	}

	err = qti_ice_init_crypto(hba->dev, ice_mmio,
								(void **)&host->crypto_priv);
	if (err) {
		pr_err("%s: Error initializing ICE, err %d\n",
					__func__, err);
		goto disable;
	}

	return 0;

disable:
	dev_warn(dev, "Disabling inline encryption support\n");
	hba->caps &= ~UFSHCD_CAP_CRYPTO;
	return 0;
}


int ufshcd_crypto_qti_ice_enable(struct ufs_qcom_host *host)
{
	int err = 0;

	if (!(host->hba->caps & UFSHCD_CAP_CRYPTO))
		return 0;

	err = qti_ice_enable(host->crypto_priv);
	if (err) {
		pr_err("%s: Error enabling crypto, err %d\n",
				__func__, err);
		ufshcd_crypto_qti_ice_disable(host);
	}
	return err;
}

int ufshcd_crypto_qti_ice_resume(struct ufs_qcom_host *host)
{
	return qti_ice_resume(host->crypto_priv);
}

void ufshcd_crypto_qti_ice_disable(struct ufs_qcom_host *host)
{
	qti_ice_disable(host->crypto_priv);
}

/*
 * Program a key into a QC ICE keyslot, or evict a keyslot.  QC ICE requires
 * vendor-specific SCM calls for this; it doesn't support the standard way.
 */
int ufshcd_crypto_ice_program_key(struct ufs_hba *hba,
				 const union ufs_crypto_cfg_entry *cfg,
			     const struct blk_crypto_key *key, int slot)
{
	union ufs_crypto_cap_entry cap;
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	int err;

	if (!(cfg->config_enable & UFS_CRYPTO_CONFIGURATION_ENABLE))
		return qti_ice_keyslot_evict(host->crypto_priv, slot);

	/* Only AES-256-XTS has been tested so far. */
	cap = hba->crypto_cap_array[cfg->crypto_cap_idx];
	if (cap.algorithm_id != UFS_CRYPTO_ALG_AES_XTS ||
	    cap.key_size != UFS_CRYPTO_KEY_SIZE_256) {
		dev_err_ratelimited(hba->dev,
				    "Unhandled crypto capability; algorithm_id=%d, key_size=%d\n",
				    cap.algorithm_id, cap.key_size);
		return -EINVAL;
	}

    err = qti_ice_keyslot_program(host->crypto_priv, key, cfg->data_unit_size,
									cfg->crypto_cap_idx, slot);
	if (err) {
		pr_err("failed to program ice - err = %d", err);
	}

	return err;
}

static int ufshcd_crypto_qti_wrapped_keyslot_program(struct blk_keyslot_manager *ksm,
					     const struct blk_crypto_key *key,
					     unsigned int slot)
{
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);
	const union ufs_crypto_cap_entry *ccap_array = hba->crypto_cap_array;
	const struct ufs_crypto_alg_entry *alg =
			&ufs_crypto_algs[key->crypto_cfg.crypto_mode];
	u8 data_unit_mask = key->crypto_cfg.data_unit_size / 512;
	union ufs_crypto_cfg_entry cfg = {};
	int i;
	int cap_idx = -1;
	int err;

	BUILD_BUG_ON(UFS_CRYPTO_KEY_SIZE_INVALID != 0);
	for (i = 0; i < hba->crypto_capabilities.num_crypto_cap; i++) {
		if (ccap_array[i].algorithm_id == alg->ufs_alg &&
		    ccap_array[i].key_size == alg->ufs_key_size &&
		    (ccap_array[i].sdus_mask & data_unit_mask)) {
			cap_idx = i;
			break;
		}
	}

	if (WARN_ON(cap_idx < 0))
		return -EOPNOTSUPP;
	
    cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable = UFS_CRYPTO_CONFIGURATION_ENABLE;

	err = ufshcd_hold(hba, false);
	if (err) {
		pr_err("%s: failed to enable clocks, err %d\n", __func__, err);
		return err;
	}

	if (hba->vops && hba->vops->program_key) {
		err = hba->vops->program_key(hba, &cfg, key, slot);
		if (err) {
			pr_err("%s: failed with err %d", __func__, err);
		}
	} else {
		pr_err("No program key op defined\n");
		err = -1;
	}

	ufshcd_release(hba);
	return err;
}

static int ufshcd_crypto_qti_wrapped_keyslot_evict(struct blk_keyslot_manager *ksm,
					   const struct blk_crypto_key *key,
					   unsigned int slot)
{
	int err = 0;
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);
	union ufs_crypto_cfg_entry cfg = {};

	err = ufshcd_hold(hba, false);
	if (err) {
		pr_err("%s: failed to enable clocks, err %d\n", __func__, err);
		return err;
	}

	if (hba->vops && hba->vops->program_key) {
		err = hba->vops->program_key(hba, &cfg, key, slot);
		if (err) {
			pr_err("%s: failed with err %d", __func__, err);
		}
	} else {
		pr_err("No program key op defined\n");
		err = -1;
	}

	ufshcd_release(hba);
	return err;
}

static int ufshcd_crypto_qti_wrapped_derive_raw_secret(struct blk_keyslot_manager *ksm,
					       const u8 *wrapped_key,
					       unsigned int wrapped_key_size,
					       u8 *secret,
					       unsigned int secret_size)
{
	int err = 0;
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	err = ufshcd_hold(hba, false);
	if (err) {
		pr_err("%s: failed to enable clocks, err %d\n", __func__, err);
		return err;
	}

	err =  qti_ice_derive_raw_secret(host->crypto_priv,
				wrapped_key, wrapped_key_size,
				secret, secret_size);

	ufshcd_release(hba);
	return err;
}

static const struct blk_ksm_ll_ops ufshcd_ksm_qti_wrapped_ops = {
	.keyslot_program	= ufshcd_crypto_qti_wrapped_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_qti_wrapped_keyslot_evict,
	.derive_raw_secret	= ufshcd_crypto_qti_wrapped_derive_raw_secret,
};

static enum blk_crypto_mode_num
ufshcd_find_blk_crypto_mode(union ufs_crypto_cap_entry cap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ufs_crypto_algs); i++) {
		BUILD_BUG_ON(UFS_CRYPTO_KEY_SIZE_INVALID != 0);
		if (ufs_crypto_algs[i].ufs_alg == cap.algorithm_id &&
		    ufs_crypto_algs[i].ufs_key_size == cap.key_size) {
			return i;
		}
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

/**
 * ufshcd_hba_init_crypto_qti_wrapped_capabilities - Read crypto capabilities,
 *             init crypto fields in hba.
 * @hba: Per adapter instance
 *
 * Return: 0 if crypto was initialized or is not supported, else a -errno value.
 */
int ufshcd_hba_init_crypto_qti_wrapped_capabilities(struct ufs_hba *hba)
{
	int cap_idx;
	int err = 0;
	enum blk_crypto_mode_num blk_mode_num;

	/*
	 * Don't use crypto if either the hardware doesn't advertise the
	 * standard crypto capability bit *or* if the vendor specific driver
	 * hasn't advertised that crypto is supported.
	 */
	if (!(hba->capabilities & MASK_CRYPTO_SUPPORT) ||
	    !(hba->caps & UFSHCD_CAP_CRYPTO))
		goto out;

	hba->crypto_capabilities.reg_val =
			cpu_to_le32(ufshcd_readl(hba, REG_UFS_CCAP));
	hba->crypto_cfg_register =
		(u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		devm_kcalloc(hba->dev, hba->crypto_capabilities.num_crypto_cap,
			     sizeof(hba->crypto_cap_array[0]), GFP_KERNEL);
	if (!hba->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	/* The actual number of configurations supported is (CFGC+1) */
	err = blk_ksm_init(&hba->ksm,
			   hba->crypto_capabilities.config_count + 1);
	if (err)
		goto out_free_caps;

	hba->ksm.ksm_ll_ops = ufshcd_ksm_qti_wrapped_ops;
	/* UFS only supports 8 bytes for any DUN */
	hba->ksm.max_dun_bytes_supported = 8;
	hba->ksm.features = BLK_CRYPTO_FEATURE_WRAPPED_KEYS;
	hba->ksm.dev = hba->dev;

	/*
	 * Cache all the UFS crypto capabilities and advertise the supported
	 * crypto modes and data unit sizes to the block layer.
	 */
	for (cap_idx = 0; cap_idx < hba->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		hba->crypto_cap_array[cap_idx].reg_val =
			cpu_to_le32(ufshcd_readl(hba,
						 REG_UFS_CRYPTOCAP +
						 cap_idx * sizeof(__le32)));
		blk_mode_num = ufshcd_find_blk_crypto_mode(
						hba->crypto_cap_array[cap_idx]);
		if (blk_mode_num != BLK_ENCRYPTION_MODE_INVALID)
			hba->ksm.crypto_modes_supported[blk_mode_num] |=
				hba->crypto_cap_array[cap_idx].sdus_mask * 512;
	}

	return 0;

out_free_caps:
	devm_kfree(hba->dev, hba->crypto_cap_array);
out:
	/* Indicate that init failed by clearing UFSHCD_CAP_CRYPTO */
	hba->caps &= ~UFSHCD_CAP_CRYPTO;
	return err;
}
