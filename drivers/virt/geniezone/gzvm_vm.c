// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/arm-smccc.h>
#include <linux/file.h>
#include <linux/kdev_t.h>
#include <linux/kvm_host.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "gzvm.h"

static DEFINE_MUTEX(gzvm_list_lock);
static LIST_HEAD(gzvm_list);


/**
 * @brief Translate gfn (guest ipa) to pfn (host pa), result is in @pfn
 *
 * Leverage KVM's `gfn_to_pfn_memslot`. Because `gfn_to_pfn_memslot` needs
 * kvm_memory_slot as parameter, this function populates necessary fileds
 * for calling `gfn_to_pfn_memslot`.
 *
 * @retval 0 succeed
 * @retval -EFAULT failed to convert
 */
int gzvm_gfn_to_pfn_memslot(struct gzvm_memslot *memslot, u64 gfn, u64 *pfn)
{
	hfn_t __pfn;
	struct kvm_memory_slot kvm_slot = {0};

	kvm_slot.base_gfn = memslot->base_gfn;
	kvm_slot.npages = memslot->npages;
	kvm_slot.dirty_bitmap = NULL;
	kvm_slot.userspace_addr = memslot->userspace_addr;
	kvm_slot.flags = memslot->flags;
	kvm_slot.id = memslot->slot_id;
	kvm_slot.as_id = 0;

	__pfn = gfn_to_pfn_memslot(&kvm_slot, gfn);
	if (is_error_noslot_pfn(__pfn)) {
		*pfn = 0;
		return -EFAULT;
	}

	*pfn = __pfn;
	return 0;
}

/**
 * @brief Populate pa to buffer until full
 *
 * @return int how much pages we've fill in, negative if error
 */
static int fill_constituents(struct mem_region_addr_range *consti,
			     int *consti_cnt, int max_nr_consti, gfn_t gfn,
			     u32 total_pages, struct gzvm_memslot *slot)
{
	int i, nr_pages;
	hfn_t pfn, prev_pfn;
	gfn_t gfn_end;

	if (unlikely(total_pages == 0))
		return -EINVAL;
	gfn_end = gfn + total_pages;

	/* entry 0 */
	if (gzvm_gfn_to_pfn_memslot(slot, gfn, &pfn) != 0)
		return -EFAULT;
	consti[0].address = PFN_PHYS(pfn);
	consti[0].pg_cnt = 1;
	gfn++;
	prev_pfn = pfn;
	i = 0;
	nr_pages = 1;
	while (i < max_nr_consti && gfn < gfn_end) {
		if (gzvm_gfn_to_pfn_memslot(slot, gfn, &pfn) != 0)
			return -EFAULT;
		if (pfn == (prev_pfn + 1)) {
			consti[i].pg_cnt++;
		} else {
			i++;
			if (i >= max_nr_consti)
				break;
			consti[i].address = PFN_PHYS(pfn);
			consti[i].pg_cnt = 1;
		}
		prev_pfn = pfn;
		gfn++;
		nr_pages++;
	}
	if (i == max_nr_consti)
		*consti_cnt = i;
	else
		*consti_cnt = (i + 1);

	return nr_pages;
}

/**
 * @brief Register memory region to GZ
 *
 * @param gzvm
 * @param memslot
 * @return int
 */
static int
register_memslot_addr_range(struct gzvm *gzvm, struct gzvm_memslot *memslot)
{
	struct gzvm_memory_region_ranges *region;
	u32 buf_size;
	int max_nr_consti, remain_pages;
	gfn_t gfn, gfn_end;

	buf_size = PAGE_SIZE * 2;
	region = alloc_pages_exact(buf_size, GFP_KERNEL);
	if (!region)
		return -ENOMEM;
	max_nr_consti = (buf_size - sizeof(*region)) /
			sizeof(struct mem_region_addr_range);

	region->slot = memslot->slot_id;
	remain_pages = memslot->npages;
	gfn = memslot->base_gfn;
	gfn_end = gfn + remain_pages;
	while (gfn < gfn_end) {
		struct arm_smccc_res res;
		int nr_pages;

		nr_pages = fill_constituents(region->constituents,
					     &region->constituent_cnt,
					     max_nr_consti, gfn,
					     remain_pages, memslot);
		region->gpa = PFN_PHYS(gfn);
		region->total_pages = nr_pages;

		remain_pages -= nr_pages;
		gfn += nr_pages;

		gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_MEMREGION, gzvm->vm_id,
			     buf_size, virt_to_phys(region), 0, 0, 0, 0, &res);

		if (res.a0 != 0) {
			pr_err("Failed to register memregion to hypervisor\n");
			free_pages_exact(region, buf_size);
			return -EFAULT;
		}
	}
	free_pages_exact(region, buf_size);
	return 0;
}

/**
 * @brief Set memory region of guest
 *
 * @param gzvm struct gzvm
 * @param mem struct gzvm_userspace_memory_region: input from user
 * @retval -EXIO memslot is out-of-range
 * @retval -EFAULT  cannot find corresponding vma
 * @retval -EINVAL  region size and vma size does not match
 */
static int gzvm_vm_ioctl_set_memory_region(struct gzvm *gzvm,
				struct gzvm_userspace_memory_region *mem)
{
	struct vm_area_struct *vma;
	struct gzvm_memslot *memslot;
	unsigned long size;
	__u32 slot;

	slot = mem->slot;
	if (slot >= GZVM_MAX_MEM_REGION)
		return -ENXIO;
	memslot = &gzvm->memslot[slot];

	vma = vma_lookup(gzvm->mm, mem->userspace_addr);
	if (!vma)
		return -EFAULT;

	size = vma->vm_end - vma->vm_start;
	if (size != mem->memory_size)
		return -EINVAL;

	memslot->base_gfn = __phys_to_pfn(mem->guest_phys_addr);
	memslot->npages = size >> PAGE_SHIFT;
	memslot->userspace_addr = mem->userspace_addr;
	memslot->vma = vma;
	memslot->flags = mem->flags;
	memslot->slot_id = mem->slot;
	return register_memslot_addr_range(gzvm, memslot);
}

static int gzvm_vm_ioctl_irq_line(struct gzvm *gzvm,
				  struct gzvm_irq_level *irq_level)
{
	u32 irq = irq_level->irq;
	unsigned int irq_type, vcpu_idx, irq_num;
	bool level = irq_level->level;

	irq_type = (irq >> GZVM_IRQ_TYPE_SHIFT) & GZVM_IRQ_TYPE_MASK;
	vcpu_idx = (irq >> GZVM_IRQ_VCPU_SHIFT) & GZVM_IRQ_VCPU_MASK;
	vcpu_idx += ((irq >> GZVM_IRQ_VCPU2_SHIFT) & GZVM_IRQ_VCPU2_MASK) *
		(GZVM_IRQ_VCPU_MASK + 1);
	irq_num = (irq >> GZVM_IRQ_NUM_SHIFT) & GZVM_IRQ_NUM_MASK;

	return gzvm_vgic_inject_irq(gzvm, vcpu_idx, irq_num, irq_type, level);
}

static int gzvm_vm_ioctl_create_device(struct gzvm *gzvm, void __user *argp)
{
	struct gzvm_create_device *gzvm_dev;
	void *dev_data = NULL;
	struct arm_smccc_res res = {0};
	int ret;

	gzvm_dev = (struct gzvm_create_device *)alloc_pages_exact(PAGE_SIZE,
								  GFP_KERNEL);
	if (!gzvm_dev)
		return -ENOMEM;
	if (copy_from_user(gzvm_dev, argp, sizeof(*gzvm_dev))) {
		ret = -EFAULT;
		goto err_free_dev;
	}

	if (gzvm_dev->attr_addr != 0 && gzvm_dev->attr_size != 0) {
		size_t attr_size = gzvm_dev->attr_size;
		void __user *attr_addr = (void __user *)gzvm_dev->attr_addr;

		/* Size of device specific data should not be over a page. */
		if (attr_size > PAGE_SIZE)
			return -EINVAL;

		dev_data = alloc_pages_exact(attr_size, GFP_KERNEL);
		if (!dev_data) {
			ret = -ENOMEM;
			goto err_free_dev;
		}

		if (copy_from_user(dev_data, attr_addr, attr_size)) {
			ret = -EFAULT;
			goto err_free_dev_data;
		}
		gzvm_dev->attr_addr = virt_to_phys(dev_data);
	}

	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_DEVICE, gzvm->vm_id,
				   virt_to_phys(gzvm_dev), 0, 0, 0, 0, 0, &res);
err_free_dev_data:
	if (dev_data)
		free_pages_exact(dev_data, 0);
err_free_dev:
	free_pages_exact(gzvm_dev, 0);
	return ret;
}

static int gzvm_vm_enable_cap_hyp(struct gzvm *gzvm,
				  struct gzvm_enable_cap *cap,
				  struct arm_smccc_res *res)
{
	int ret;

	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_ENABLE_CAP, gzvm->vm_id,
				   cap->cap, cap->args[0], cap->args[1],
				   cap->args[2], cap->args[3], cap->args[4],
				   res);
	return ret;
}

/**
 * @brief Get pvmfw size from hypervisor, return in x1, and return to userspace
 *        in args[1].
 * @retval 0 succeed
 * @retval -EINVAL hypervisor return invalid results
 * @retval -EFAULT fail to copy back to userspace buffer
 */
static int gzvm_vm_ioctl_get_pvmfw_size(struct gzvm *gzvm,
					struct gzvm_enable_cap *cap,
					void __user *argp)
{
	struct arm_smccc_res res = {0};

	if (gzvm_vm_enable_cap_hyp(gzvm, cap, &res) != 0)
		return -EINVAL;

	cap->args[1] = res.a1;
	if (copy_to_user(argp, cap, sizeof(*cap)))
		return -EFAULT;

	return 0;
}

/**
 * @brief Proceed GZVM_CAP_ARM_PROTECTED_VM's subcommands
 * @retval 0 succeed
 * @retval -EINVAL invalid subcommand or arguments
 */
static int gzvm_vm_ioctl_cap_pvm(struct gzvm *gzvm, struct gzvm_enable_cap *cap,
				 void __user *argp)
{
	int ret = -EINVAL;
	struct arm_smccc_res res = {0};

	switch (cap->args[0]) {
	case GZVM_CAP_ARM_PVM_SET_PVMFW_IPA:
		ret = gzvm_vm_enable_cap_hyp(gzvm, cap, &res);
		break;
	case GZVM_CAP_ARM_PVM_GET_PVMFW_SIZE:
		ret = gzvm_vm_ioctl_get_pvmfw_size(gzvm, cap, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int gzvm_vm_ioctl_enable_cap(struct gzvm *gzvm,
				    struct gzvm_enable_cap *cap,
				    void __user *argp)
{
	int ret = -EINVAL;

	switch (cap->cap) {
	case GZVM_CAP_ARM_PROTECTED_VM:
		ret = gzvm_vm_ioctl_cap_pvm(gzvm, cap, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * @brief ioctl handler of VM FD
 */
static long gzvm_vm_ioctl(struct file *filp, unsigned int ioctl,
			  unsigned long arg)
{
	long ret = -ENOIOCTLCMD;
	void __user *argp = (void __user *)arg;
	struct gzvm *gzvm = filp->private_data;

	switch (ioctl) {
	case GZVM_CHECK_EXTENSION:
		ret = gzvm_dev_ioctl_check_extension(gzvm, arg);
		break;
	case GZVM_CREATE_VCPU:
		ret = gzvm_vm_ioctl_create_vcpu(gzvm, arg);
		break;
	case GZVM_SET_USER_MEMORY_REGION: {
		struct gzvm_userspace_memory_region userspace_mem;

		ret = -EFAULT;
		if (copy_from_user(&userspace_mem, argp,
						sizeof(userspace_mem)))
			goto out;
		ret = gzvm_vm_ioctl_set_memory_region(gzvm, &userspace_mem);
		break;
	}
	case GZVM_IRQ_LINE: {
		struct gzvm_irq_level irq_event;

		ret = -EFAULT;
		if (copy_from_user(&irq_event, argp, sizeof(irq_event)))
			goto out;

		ret = gzvm_vm_ioctl_irq_line(gzvm, &irq_event);
		break;
	}
	case GZVM_CREATE_DEVICE: {
		ret = gzvm_vm_ioctl_create_device(gzvm, argp);
		break;
	}
	case GZVM_IOEVENTFD: {
		struct gzvm_ioeventfd data;

		ret = -EFAULT;
		if (copy_from_user(&data, argp, sizeof(data)))
			goto out;
		ret = gzvm_ioeventfd(gzvm, &data);
		break;
	}
	case GZVM_IRQFD: {
		struct gzvm_irqfd data;

		ret = -EFAULT;
		if (copy_from_user(&data, argp, sizeof(data)))
			goto out;
		ret = gzvm_irqfd(gzvm, &data);
		break;
	}
	case GZVM_ENABLE_CAP: {
		struct gzvm_enable_cap cap;

		ret = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			goto out;

		ret = gzvm_vm_ioctl_enable_cap(gzvm, &cap, argp);
		break;
	}
	default:
		pr_debug("%s invalid ioctl=0x%x\n", __func__, ioctl);
		ret = -ENOIOCTLCMD;
	}
out:
	return ret;
}

/**
 * @brief Destroy all vcpus
 *
 * @param gzvm vm struct that owns the vcpus
 * Caller has to hold the vm lock
 */
static void gzvm_destroy_vcpus(struct gzvm *gzvm)
{
	int i;

	for (i = 0; i < GZVM_MAX_VCPUS; i++) {
		gzvm_destroy_vcpu(gzvm->vcpus[i]);
		gzvm->vcpus[i] = NULL;
	}
}

static int gzvm_destroy_vm_hyp(gzvm_id_t vm_id)
{
	struct arm_smccc_res res;

	gzvm_hypcall_wrapper(MT_HVC_GZVM_DESTROY_VM, vm_id, 0, 0, 0, 0, 0, 0,
			     &res);

	return 0;
}

static void gzvm_destroy_vm(struct gzvm *gzvm)
{
	pr_info("VM-%u is going to be destroyed\n", gzvm->vm_id);

	mutex_lock(&gzvm->lock);

	gzvm_destroy_vcpus(gzvm);
	gzvm_destroy_vm_hyp(gzvm->vm_id);

	mutex_lock(&gzvm_list_lock);
	list_del(&gzvm->vm_list);
	mutex_unlock(&gzvm_list_lock);

	mutex_unlock(&gzvm->lock);

	kfree(gzvm);
}

static int gzvm_vm_release(struct inode *inode, struct file *filp)
{
	struct gzvm *gzvm = filp->private_data;

	gzvm_destroy_vm(gzvm);
	return 0;
}

static const struct file_operations gzvm_vm_fops = {
	.release        = gzvm_vm_release,
	.unlocked_ioctl = gzvm_vm_ioctl,
	.llseek		= noop_llseek,
};

static int gzvm_create_vm_hyp(void)
{
	struct arm_smccc_res res;

	gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_VM, 0, 0, 0, 0, 0, 0, 0, &res);

	if (res.a0 != 0)
		return -EFAULT;
	return res.a1;
}

static struct gzvm *gzvm_create_vm(unsigned long vm_type)
{
	int ret;
	struct gzvm *gzvm;

	gzvm = kzalloc(sizeof(struct gzvm), GFP_KERNEL);
	if (!gzvm)
		return ERR_PTR(-ENOMEM);

	ret = gzvm_create_vm_hyp();
	if (ret < 0)
		goto err;

	gzvm->vm_id = ret;
	gzvm->mm = current->mm;
	mutex_init(&gzvm->lock);
	INIT_LIST_HEAD(&gzvm->devices);
	mutex_init(&gzvm->irq_lock);
	ret = gzvm_init_eventfd(gzvm);
	if (ret) {
		pr_err("Failed to initialize eventfd\n");
		goto err;
	}
	pr_info("VM-%u is created\n", gzvm->vm_id);

	mutex_lock(&gzvm_list_lock);
	list_add(&gzvm->vm_list, &gzvm_list);
	mutex_unlock(&gzvm_list_lock);

	return gzvm;

err:
	kfree(gzvm);
	return ERR_PTR(ret);
}

/**
 * @brief create vm fd
 *
 * @param vm_type
 * @return int fd of vm, negative if error
 */
int gzvm_dev_ioctl_create_vm(unsigned long vm_type)
{
	struct gzvm *gzvm;
	int ret;

	gzvm = gzvm_create_vm(vm_type);
	if (IS_ERR(gzvm)) {
		ret = PTR_ERR(gzvm);
		goto error;
	}

	ret = anon_inode_getfd("gzvm-vm", &gzvm_vm_fops, gzvm,
			       O_RDWR | O_CLOEXEC);
	if (ret < 0)
		goto error;

error:
	return ret;
}
