#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

/* External reference to your existing function */
extern void wlc_exec(void);

#define OBSERVER_PERIOD_MS 20

static struct timer_list observer_timer;

static void observer_timer_callback(struct timer_list *t)
{
    wlc_exec();

    mod_timer(&observer_timer, jiffies + msecs_to_jiffies(OBSERVER_PERIOD_MS));
}

static int __init micro_observer_init(void)
{
    pr_info("Micro Observer: Initializing periodic execution (20ms)\n");

    timer_setup(&observer_timer, observer_timer_callback, 0);
    
    mod_timer(&observer_timer, jiffies + msecs_to_jiffies(OBSERVER_PERIOD_MS));

    return 0;
}

static void __exit micro_observer_exit(void)
{
    pr_info("Micro Observer: Stopping timer and exiting\n");
    del_timer_sync(&observer_timer);
}

module_init(micro_observer_init);
module_exit(micro_observer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Charles Yang");
MODULE_DESCRIPTION("Periodic trigger for GPA Micro Observer");

