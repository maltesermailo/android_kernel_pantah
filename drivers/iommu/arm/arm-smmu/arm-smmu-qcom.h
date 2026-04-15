/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ARM_SMMU_QCOM_H
#define _ARM_SMMU_QCOM_H

struct dentry;

enum qcom_smmu_sva_class {
	QCOM_SMMU_SVA_CLASS_STANDARD_SVA_PASID_SUPPORTED,
	QCOM_SMMU_SVA_CLASS_DRIVER_NOT_WIRED_FOR_SVA_PASID,
	QCOM_SMMU_SVA_CLASS_PRIVATE_GPU_ONLY,
	QCOM_SMMU_SVA_CLASS_UNKNOWN,
};

enum qcom_smmu_obs_flag {
	QCOM_SMMU_OBS_STANDARD_SVA_API = BIT(0),
	QCOM_SMMU_OBS_PASID_PROGRAMMING = BIT(1),
	QCOM_SMMU_OBS_ADRENO_TTBR0 = BIT(2),
	QCOM_SMMU_OBS_ADRENO_STALL = BIT(3),
	QCOM_SMMU_OBS_ADRENO_FAULT_INFO = BIT(4),
	QCOM_SMMU_OBS_ADRENO_RESUME = BIT(5),
	QCOM_SMMU_OBS_CLIENT_GPU = BIT(6),
	QCOM_SMMU_OBS_CLIENT_NPU = BIT(7),
	QCOM_SMMU_OBS_CLIENT_PCIE = BIT(8),
};

struct qcom_smmu {
	struct arm_smmu_device smmu;
	const struct qcom_smmu_config *cfg;
	bool bypass_quirk;
	u8 bypass_cbndx;
	u32 stall_enabled;
#ifdef CONFIG_ARM_SMMU_QCOM_DEBUG
	unsigned long obs_flags;
	struct dentry *sva_debugfs_root;
#endif
};

enum qcom_smmu_impl_reg_offset {
	QCOM_SMMU_TBU_PWR_STATUS,
	QCOM_SMMU_STATS_SYNC_INV_TBU_ACK,
	QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR,
};

struct qcom_smmu_config {
	const u32 *reg_offset;
};

struct qcom_smmu_match_data {
	const struct qcom_smmu_config *cfg;
	const struct arm_smmu_impl *impl;
	const struct arm_smmu_impl *adreno_impl;
};

irqreturn_t qcom_smmu_context_fault(int irq, void *dev);

#ifdef CONFIG_ARM_SMMU_QCOM_DEBUG
unsigned long qcom_smmu_static_obs_flags(struct qcom_smmu *qsmmu);
enum qcom_smmu_sva_class qcom_smmu_classify(struct qcom_smmu *qsmmu,
					    unsigned long runtime_flags);
const char *qcom_smmu_class_name(enum qcom_smmu_sva_class class);
void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *smmu);
int qcom_smmu_debugfs_register(struct qcom_smmu *qsmmu);
void qcom_smmu_log_status(struct qcom_smmu *qsmmu);
int qcom_tbu_probe(struct platform_device *pdev);
#else
static inline void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *smmu) { }
static inline unsigned long qcom_smmu_static_obs_flags(struct qcom_smmu *qsmmu) { return 0; }
static inline enum qcom_smmu_sva_class qcom_smmu_classify(struct qcom_smmu *qsmmu,
							   unsigned long runtime_flags)
{
	return QCOM_SMMU_SVA_CLASS_UNKNOWN;
}
static inline const char *qcom_smmu_class_name(enum qcom_smmu_sva_class class)
{
	return "unknown";
}
static inline int qcom_smmu_debugfs_register(struct qcom_smmu *qsmmu) { return -EINVAL; }
static inline void qcom_smmu_log_status(struct qcom_smmu *qsmmu) { }
static inline int qcom_tbu_probe(struct platform_device *pdev) { return -EINVAL; }
#endif

#endif /* _ARM_SMMU_QCOM_H */
