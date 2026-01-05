// SPDX-License-Identifier: GPL-2.0
/*
 *  syscore.c - Execution of system core operations.
 *
 *  Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 */

#include <linux/syscore_ops.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <trace/events/power.h>
#include <linux/wakeup_reason.h>

static LIST_HEAD(syscore_list);
static DEFINE_MUTEX(syscore_lock);

/**
 * register_syscore - Register a set of system core operations.
 * @syscore: System core operations to register.
 */
void register_syscore(struct syscore *syscore)
{
	mutex_lock(&syscore_lock);
	list_add_tail(&syscore->node, &syscore_list);
	mutex_unlock(&syscore_lock);
}
EXPORT_SYMBOL_GPL(register_syscore);

/**
 * unregister_syscore - Unregister a set of system core operations.
 * @syscore: System core operations to unregister.
 */
void unregister_syscore(struct syscore *syscore)
{
	mutex_lock(&syscore_lock);
	list_del(&syscore->node);
	mutex_unlock(&syscore_lock);
}
EXPORT_SYMBOL_GPL(unregister_syscore);

#ifdef CONFIG_PM_SLEEP
/**
 * syscore_suspend - Execute all the registered system core suspend callbacks.
 *
 * This function is executed with one CPU on-line and disabled interrupts.
 */
int syscore_suspend(void)
{
	struct syscore *syscore;
	int ret = 0;

	trace_suspend_resume(TPS("syscore_suspend"), 0, true);
	pm_pr_dbg("Checking wakeup interrupts\n");

	/* Return error code if there are any wakeup interrupts pending. */
	if (pm_wakeup_pending())
		return -EBUSY;

	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled before system core suspend.\n");

	list_for_each_entry_reverse(syscore, &syscore_list, node)
		if (syscore->ops->suspend) {
			pm_pr_dbg("Calling %pS\n", syscore->ops->suspend);
			ret = syscore->ops->suspend(syscore->data);
			if (ret)
				goto err_out;
			WARN_ONCE(!irqs_disabled(),
				"Interrupts enabled after %pS\n",
				syscore->ops->suspend);
		}

	trace_suspend_resume(TPS("syscore_suspend"), 0, false);
	return 0;

 err_out:
<<<<<<< HEAD   (9355d9a2a6da36e11a2db1f616b5acdeda4002ee ANDROID: syscore: fix missing switch to %pS printk)
	log_suspend_abort_reason("System core suspend callback %pS failed",
		ops->suspend);
	pr_err("PM: System core suspend callback %pS failed.\n", ops->suspend);
||||||| BASE   (66a1025f7f0bc00404ec6357af68815c70dadae2 Merge tag 'soc-newsoc-6.19' of git://git.kernel.org/pub/scm/)
	pr_err("PM: System core suspend callback %pS failed.\n", ops->suspend);
=======
	pr_err("PM: System core suspend callback %pS failed.\n",
	       syscore->ops->suspend);
>>>>>>> BRANCH (208eed95fc710827b100266c9450ae84d46727bd Merge tag 'soc-drivers-6.19' of git://git.kernel.org/pub/scm)

	list_for_each_entry_continue(syscore, &syscore_list, node)
		if (syscore->ops->resume)
			syscore->ops->resume(syscore->data);

	return ret;
}
EXPORT_SYMBOL_GPL(syscore_suspend);

/**
 * syscore_resume - Execute all the registered system core resume callbacks.
 *
 * This function is executed with one CPU on-line and disabled interrupts.
 */
void syscore_resume(void)
{
	struct syscore *syscore;

	trace_suspend_resume(TPS("syscore_resume"), 0, true);
	WARN_ONCE(!irqs_disabled(),
		"Interrupts enabled before system core resume.\n");

	list_for_each_entry(syscore, &syscore_list, node)
		if (syscore->ops->resume) {
			pm_pr_dbg("Calling %pS\n", syscore->ops->resume);
			syscore->ops->resume(syscore->data);
			WARN_ONCE(!irqs_disabled(),
				"Interrupts enabled after %pS\n",
				syscore->ops->resume);
		}
	trace_suspend_resume(TPS("syscore_resume"), 0, false);
}
EXPORT_SYMBOL_GPL(syscore_resume);
#endif /* CONFIG_PM_SLEEP */

/**
 * syscore_shutdown - Execute all the registered system core shutdown callbacks.
 */
void syscore_shutdown(void)
{
	struct syscore *syscore;

	mutex_lock(&syscore_lock);

	list_for_each_entry_reverse(syscore, &syscore_list, node)
		if (syscore->ops->shutdown) {
			if (initcall_debug)
				pr_info("PM: Calling %pS\n",
					syscore->ops->shutdown);
			syscore->ops->shutdown(syscore->data);
		}

	mutex_unlock(&syscore_lock);
}
