#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Estudante");
MODULE_DESCRIPTION("Proc Driver");
MODULE_VERSION("0.0.1");

#define PROCFS_NAME "helloworld"

static int greeting_size = 256;
static char *initial_greeting = "Hello, World!";
static char *greetingp = NULL;

static ssize_t procfile_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t procfile_write(struct file *, const char __user *, size_t, loff_t *);

static struct proc_ops proc_file_fops = {
    .proc_read  = procfile_read,
    .proc_write = procfile_write
};

static struct proc_dir_entry *proc_file;

static int procdrv_init(void)
{
    pr_info("Inserting the Proc module\n");

    if (strlen(initial_greeting) >= greeting_size) {
        pr_alert("Initial greeting is too long, maximum allowed is %zu characters\n", greeting_size - 1);
        return -EINVAL;
    }

    greetingp = kmalloc(greeting_size, GFP_KERNEL);
    if (greetingp == NULL) {
        pr_alert("Failed to allocate memory for greeting\n");
        return -ENOMEM;
    }
    strcpy(greetingp, initial_greeting);

    proc_file = proc_create(PROCFS_NAME, 0644, NULL, &proc_file_fops);
    if (proc_file == NULL) {
        pr_alert("Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }
    pr_info("/proc/%s created\n", PROCFS_NAME);

    return 0;
}

static void procdrv_exit(void)
{
    kfree(greetingp);
    proc_remove(proc_file);
    pr_info("Removed the Proc module\n");
}

static ssize_t procfile_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    static int reads = 0;

    pr_info("Calling procfile_read\n");

    if (*offset > 0)
        return 0;

    char tmpbuf[256];
    sprintf(tmpbuf, "%s, %d reads\n", greetingp, ++reads);
    int tmplen = strlen(tmpbuf);

    int error_count = copy_to_user(buffer, tmpbuf, tmplen);
    if (error_count != 0) {
        pr_alert("Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }

    *offset = tmplen;
    return tmplen;
}

static ssize_t procfile_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    pr_alert("Calling procfile_write -- beware, this is unusual\n");

    if (len > greeting_size) {
        pr_alert("Input is too long, maximum allowed is %zu characters\n", greeting_size - 1);
        return -EINVAL;
    }

    int err = copy_from_user(greetingp, buffer, len);
    if (err != 0) {
        pr_alert("Failed to copy from user space\n");
        return -EFAULT;
    }

    if (greetingp[len-1] == '\n')
        greetingp[len-1] = '\0';
    greetingp[len] = '\0';
    return len;
}

module_init(procdrv_init);
module_exit(procdrv_exit);
module_param(initial_greeting, charp, 0444);
module_param(greeting_size, int, 0444);
