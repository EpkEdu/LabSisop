#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Estudante");
MODULE_DESCRIPTION("A very simple kernel module, with parameters.");
MODULE_VERSION("0.0.1");

static char *string_par = NULL;
static int int_par = -1;

module_param(string_par, charp, 0644);
module_param(int_par, int, 0644);

static int mymodule_init(void)
{
    if (string_par == NULL || int_par == -1) {
        printk(KERN_ALERT "mymodule_param not loaded. Pass a string and an integer!\n");
        printk(KERN_ALERT "Example: modprobe mymodule_param string_par=\"hello world\" int_par=1234\n");
        return -EINVAL;
    }
    printk(KERN_INFO "mymodule_param loaded.\n");
    printk(KERN_NOTICE "string_par: %s, int_par: %d\n", string_par, int_par);
    return 0;
}

static void mymodule_exit(void)
{
    printk(KERN_INFO "mymodule_param unloaded.\n");
}

module_init(mymodule_init);
module_exit(mymodule_exit);