// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/arm-smccc.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/kdev_t.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "gzvm.h"

static void (*invoke_gzvm_fn)(unsigned long, unsigned long, unsigned long,
			      unsigned long, unsigned long, unsigned long,
			      unsigned long, unsigned long,
			      struct arm_smccc_res *);

static void gzvm_hvc(unsigned long a0, unsigned long a1, unsigned long a2,
		      unsigned long a3, unsigned long a4, unsigned long a5,
		      unsigned long a6, unsigned long a7,
		      struct arm_smccc_res *res)
{
	arm_smccc_hvc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

static void gzvm_smc(unsigned long a0, unsigned long a1, unsigned long a2,
		      unsigned long a3, unsigned long a4, unsigned long a5,
		      unsigned long a6, unsigned long a7,
		      struct arm_smccc_res *res)
{
	arm_smccc_smc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

static int gzvm_probe_conduit(void)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == 0) {
		invoke_gzvm_fn = gzvm_hvc;
		return 0;
	}

	arm_smccc_smc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == 0) {
		invoke_gzvm_fn = gzvm_smc;
		return 0;
	}

	return -ENXIO;
}

/**
 * @brief geniezone hypercall wrapper
 * @return int geniezone's return value will be converted to Linux errno
 */
int gzvm_hypcall_wrapper(unsigned long a0, unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4, unsigned long a5,
			 unsigned long a6, unsigned long a7,
			 struct arm_smccc_res *res)
{
	invoke_gzvm_fn(a0, a1, a2, a3, a4, a5, a6, a7, res);
	return gz_err_to_errno(res->a0);
}

/**
 * @brief Convert geniezone return value to standard errno
 *
 * @param err return value from geniezone hypercall (a0)
 * @return int errno
 */
int gz_err_to_errno(unsigned long err)
{
	int gz_err = (int) err;

	switch (gz_err) {
	case 0:
		return 0;
	case ERR_NO_MEMORY:
		return -ENOMEM;
	case ERR_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case ERR_NOT_IMPLEMENTED:
		return -EOPNOTSUPP;
	case ERR_FAULT:
		return -EFAULT;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int gzvm_cap_arm_vm_ipa_size(void __user *argp)
{
	u64 value = CONFIG_ARM64_PA_BITS;

	if (copy_to_user(argp, &value, sizeof(u64)))
		return -EFAULT;

	return 0;
}

/**
 * @brief Check if given capability is support or not
 *
 * @param args in/out u64 pointer from userspace
 * @retval 0: support, no error
 * @retval -EOPNOTSUPP: not support
 * @retval -EFAULT: failed to get data from userspace
 */
long gzvm_dev_ioctl_check_extension(struct gzvm *gzvm, unsigned long args)
{
	int ret = -EOPNOTSUPP;
	__u64 cap, success = 1;
	void __user *argp = (void __user *) args;

	if (copy_from_user(&cap, argp, sizeof(uint64_t)))
		return -EFAULT;

	switch (cap) {
	case GZVM_CAP_ARM_PROTECTED_VM:
		if (copy_to_user(argp, &success, sizeof(uint64_t)))
			return -EFAULT;
		ret = 0;
		break;
	case GZVM_CAP_ARM_VM_IPA_SIZE:
		ret = gzvm_cap_arm_vm_ipa_size(argp);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static long gzvm_dev_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long user_args)
{
	long ret = -ENOIOCTLCMD;

	switch (cmd) {
	case GZVM_CREATE_VM:
		ret = gzvm_dev_ioctl_create_vm(user_args);
		break;
	case GZVM_CHECK_EXTENSION:
		if (!user_args)
			return -EINVAL;
		ret = gzvm_dev_ioctl_check_extension(NULL, user_args);
		break;
	default:
		pr_debug("%s invalid ioctl=0x%x\n", __func__, cmd);
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static const struct file_operations gzvm_chardev_ops = {
	.unlocked_ioctl = gzvm_dev_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice gzvm_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODULE_NAME,
	.fops = &gzvm_chardev_ops,
};

static int gzvm_init(void)
{
	int ret;

	if (gzvm_probe_conduit() != 0)
		return -ENXIO;

	ret = gzvm_irqfd_init();
	if (ret)
		return ret;

	ret = misc_register(&gzvm_dev);
	if (ret)
		return ret;

	return 0;
}

static void gzvm_exit(void)
{
	gzvm_irqfd_exit();
	misc_deregister(&gzvm_dev);
}

module_init(gzvm_init);
module_exit(gzvm_exit);

MODULE_AUTHOR("MediaTek");
MODULE_DESCRIPTION("GenieZone interface for VMM");
MODULE_LICENSE("GPL");
