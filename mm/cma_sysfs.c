// SPDX-License-Identifier: GPL-2.0
/*
 * CMA SysFS Interface
 *
 * Copyright (c) 2021 Minchan Kim <minchan@kernel.org>
 */

#include <linux/cma.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "cma.h"

void cma_sysfs_account_success_pages(struct cma *cma, size_t count)
{
	atomic64_add(count, &cma->nr_pages_succeeded);
}

void cma_sysfs_account_fail_pages(struct cma *cma, size_t count)
{
	atomic64_add(count, &cma->nr_pages_failed);
}

#define CMA_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define to_cma_kobject(x) container_of(x, struct cma_kobject, kobj)

static ssize_t alloc_pages_success_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct cma_kobject *cma_kobj = to_cma_kobject(kobj);
	struct cma *cma = cma_kobj->cma;

	return sysfs_emit(buf, "%llu\n",
			  atomic64_read(&cma->nr_pages_succeeded));
}
CMA_ATTR_RO(alloc_pages_success);

static ssize_t alloc_pages_fail_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct cma_kobject *cma_kobj = to_cma_kobject(kobj);
	struct cma *cma = cma_kobj->cma;

	return sysfs_emit(buf, "%llu\n", atomic64_read(&cma->nr_pages_failed));
}
CMA_ATTR_RO(alloc_pages_fail);

static void cma_kobj_release(struct kobject *kobj)
{
	struct cma_kobject *cma_kobj = to_cma_kobject(kobj);
	struct cma *cma = cma_kobj->cma;

	kfree(cma_kobj);
	cma->kobj = NULL;
}

static struct attribute *cma_attrs[] = {
	&alloc_pages_success_attr.attr,
	&alloc_pages_fail_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cma);

static struct kobject *cma_kobj_root;

static struct kobj_type cma_ktype = {
	.release = cma_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = cma_groups
};

static int __init cma_sysfs_init(void)
{
	unsigned int i;

	cma_kobj_root = kobject_create_and_add("cma", mm_kobj);
	if (!cma_kobj_root)
		return -ENOMEM;

	for (i = 0; i < cma_area_count; i++) {
		int err;
		struct cma *cma;
		struct cma_kobject *cma_kobj;

		cma_kobj = kzalloc(sizeof(*cma_kobj), GFP_KERNEL);
		if (!cma_kobj) {
			kobject_put(cma_kobj_root);
			return -ENOMEM;
		}

		cma = &cma_areas[i];
		cma->kobj = cma_kobj;
		cma_kobj->cma = cma;
		err = kobject_init_and_add(&cma_kobj->kobj, &cma_ktype,
					   cma_kobj_root, "%s", cma->name);
		if (err) {
			kobject_put(&cma_kobj->kobj);
			kobject_put(cma_kobj_root);
			return err;
		}
	}

	return 0;
}
subsys_initcall(cma_sysfs_init);
