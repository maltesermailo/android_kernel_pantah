// SPDX-License-Identifier: GPL-2.0
// ChromeOS EC keyboard driver

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

// Wait queue for the userspace process to block on
static DECLARE_WAIT_QUEUE_HEAD(read_wq);
static int event_ready = 0;
static int pm_event_code = 0;

// The callback function that gets called by the kernel PM core
static int suspend_notifier_cb(struct notifier_block *nb, unsigned long action, void *data) {
    switch (action) {
    case PM_SUSPEND_PREPARE:
        pr_info("suspend_notifier: PM_SUSPEND_PREPARE received!\n");
        pm_event_code = 1; // 1 for suspend
        event_ready = 1;
        wake_up_interruptible(&read_wq);
        break;
    case PM_POST_SUSPEND:
        pr_info("suspend_notifier: PM_POST_SUSPEND received!\n");
        pm_event_code = 2; // 2 for resume
        event_ready = 1;
        wake_up_interruptible(&read_wq);
        break;
    default:
        break;
    }
    return NOTIFY_OK;
}

static struct notifier_block suspend_notifier = {
    .notifier_call = suspend_notifier_cb,
};

// File operations for the character device
static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    int ret;
    // Block until the notifier callback signals an event
    wait_event_interruptible(read_wq, event_ready);
    if (copy_to_user(buf, &pm_event_code, sizeof(pm_event_code))) {
        ret = -EFAULT;
    } else {
        ret = sizeof(pm_event_code);
    }
    event_ready = 0; // Reset for next event
    return ret;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = my_read,
};

static struct miscdevice my_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "suspend_notifier",
    .fops  = &fops,
};

// Module initialization
static int __init my_notifier_init(void) {
    int ret;
    ret = misc_register(&my_misc_device);
    if (ret) {
        pr_err("my_notifier: Failed to register misc device\n");
        return ret;
    }
    ret = register_pm_notifier(&suspend_notifier);
    if (ret) {
        pr_err("my_notifier: Failed to register PM notifier\n");
        misc_deregister(&my_misc_device);
        return ret;
    }
    pr_info("my_notifier: Module loaded and notifier registered.\n");
    return 0;
}

// Module cleanup
static void __exit my_notifier_exit(void) {
    unregister_pm_notifier(&suspend_notifier);
    misc_deregister(&my_misc_device);
    pr_info("my_notifier: Module unloaded.\n");
}

module_init(my_notifier_init);
module_exit(my_notifier_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple suspend/resume notifier module.");
