#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Estudante");
MODULE_DESCRIPTION("KMOD Exemplo");
MODULE_VERSION("0.0.1");

static int hello_init(void)
{
    printk(KERN_EMERG "KERN_EMERG: Used for emergency messages, usually those that precede a crash.\n");
    printk(KERN_ALERT "KERN_ALERT: A situation requiring immediate action.\n");
    printk(KERN_CRIT "KERN_CRIT: Critical conditions, often related to serious hardware or software failures.\n");
    printk(KERN_ERR "KERN_ERR: Used to report error conditions; device drivers often use KERN_ERR to report hardware difficulties.\n");
    printk(KERN_WARNING "KERN_WARNING: Warnings about problematic situations that do not, in themselves, create serious problems with the system.\n");
    printk(KERN_NOTICE "KERN_NOTICE: Situations that are normal, but still worthy of note. A number of security-related conditions are reported at this level.\n");
    printk(KERN_INFO "KERN_INFO: Informational messages. Many drivers print information about the hardware they find at startup time at this level.\n");
    printk(KERN_DEBUG "KERN_DEBUG: Used for debugging messages.\n");

    return 0;
}

static void hello_exit(void)
{
    printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(hello_init);
module_exit(hello_exit);