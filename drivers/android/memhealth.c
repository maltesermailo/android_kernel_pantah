#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/tracepoint.h>
#include <trace/events/oom.h>

/* The following attribute is for testing only! */
static uint32_t test_event;

static int param_set_test_event(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	switch (val)
	{
	case 1:
		pr_info("Generating OOM-kill\n");

		/**
		* Generate OOM kill to verify that we are calling mark_victim_probe()
		* out_of_memory() -> Required to temporarily export it from `mm/oom_kill.c`
		*/
		out_of_memory(&(struct oom_control) {
			.zonelist = node_zonelist(first_memory_node, GFP_KERNEL),
			.nodemask = NULL,
			.memcg = NULL,
			.gfp_mask = GFP_KERNEL,
			.order = -1,
		});
		test_event = val;
		break;
	default:
		pr_err("Unknown event %d\n", val);
	}

	return 0;
}

static const struct kernel_param_ops param_ops_test_event = {
	.set = param_set_test_event,
	.get = param_get_uint,
};

module_param_cb(test_event, &param_ops_test_event, &test_event, 0644);

/* Memhealth module */
static DEFINE_SPINLOCK(memhealth_lock);
static int oom_count;

static struct miscdevice memhealth_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "memhealthmod",
};

static ssize_t oom_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oom_count);
}

static struct kobj_attribute dev_attr_oom_count = __ATTR_RO(oom_count);

static struct attribute *memhealth_attributes[] = {
	&dev_attr_oom_count.attr,
	NULL
};

static struct attribute_group memhealth_attr_group = {
	.name = "memhealth",
	.attrs = memhealth_attributes,
};

static void mark_victim_probe(void *data, pid_t pid)
{
	pr_info("OOM-killer killed process %d\n", pid);
	spin_lock(&memhealth_lock);
	oom_count++;
	spin_unlock(&memhealth_lock);
	sysfs_notify(mm_kobj, memhealth_attr_group.name, dev_attr_oom_count.attr.name);
}

static int __init memhealthmod_start(void)
{
	int result;

	pr_info(KERN_INFO "Loading Android test module...\n");
	result = misc_register(&memhealth_misc);
	if (result < 0)
		return -ENODEV;

	if (sysfs_create_group(mm_kobj, &memhealth_attr_group))
		pr_err("memhealth: failed to create sysfs group\n");

	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		pr_info("Hooked a probe to the mark_victim tracepoint\n");
	else
		pr_warn("Failed to hook a probe to the mark_victim tracepoint\n");

	pr_info(KERN_INFO "Android test module loaded\n");

	return 0;
}

static void __exit memhealthmod_end(void)
{
	pr_info(KERN_INFO "Unloading Android test module\n");

	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		pr_warn("Failed to unhook a probe from the mark_victim tracepoint\n");

	sysfs_remove_group(mm_kobj, &memhealth_attr_group);

	misc_deregister(&memhealth_misc);
}

module_init(memhealthmod_start);
module_exit(memhealthmod_end);

MODULE_LICENSE("GPL");
