#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/module.h>

static int __init hello_init(void)
{
    printk(KERN_EMERG "Hello, Linux Driver 80407966!\n");
    return 0;   
}
static void __exit hello_exit(void)
{
    printk(KERN_EMERG "Hello, Driver Exit!\n");  
}
module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL v2");
