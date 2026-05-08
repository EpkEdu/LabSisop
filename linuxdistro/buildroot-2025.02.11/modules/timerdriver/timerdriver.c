#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/hrtimer.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Estudante");
MODULE_DESCRIPTION("Timer Module");
MODULE_VERSION("0.0.1");

#define TIMEOUT_SEC 1
#define TIMEOUT_NSEC 0
#define PROCFS_NAME "timeouts"

static struct hrtimer hr_timer;

static struct kobject *timerdrv_kobj;
static enum hrtimer_restart timer_callback(struct hrtimer *);

static int prints = 0;
static unsigned long timeout_nsec = TIMEOUT_NSEC;

static ssize_t procfile_read(struct file *, char __user *, size_t, loff_t *);

static struct proc_ops proc_file_fops = {
    .proc_read  = procfile_read
};

static struct proc_dir_entry *proc_file;

static s64 timeout_sec = TIMEOUT_SEC;
static ssize_t timeout_sec_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t timeout_sec_store(struct kobject *, struct kobj_attribute *, const char *, size_t);

static struct kobj_attribute timeout_sec_attr = __ATTR(
    timeout_sec, 
    0660, 
    timeout_sec_show, 
    timeout_sec_store
);

static int timeout_get(char *, const struct kernel_param *);
static int timeout_set(const char *, const struct kernel_param *);

static const struct kernel_param_ops timeout_nsec_param_ops = {
    .get = timeout_get,
    .set = timeout_set
};

static int timerdrv_init(void)
{
    pr_info("Inserting the Timer module\n");

    hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    hr_timer.function = timer_callback;

    timerdrv_kobj = kobject_create_and_add("timerdrv", NULL);

    int error = sysfs_create_file(timerdrv_kobj, &timeout_sec_attr.attr);
    if (error) {
        pr_alert("Failed to create sysfs /sys/timerdrv/timeout_sec\n");
        kobject_put(timerdrv_kobj);
        return error;
    }

    proc_file = proc_create(PROCFS_NAME, 0444, NULL, &proc_file_fops);
    if (proc_file == NULL) {
        pr_alert("Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }
    pr_info("/proc/%s created\n", PROCFS_NAME);


ktime_t ktime = ktime_set(timeout_sec, timeout_nsec);
    hrtimer_start(&hr_timer, ktime, HRTIMER_MODE_REL);

    return 0;
}

static void timerdrv_exit(void)
{
    hrtimer_cancel(&hr_timer);
    proc_remove(proc_file);
    kobject_put(timerdrv_kobj);
    pr_info("Removed the Timer module\n");
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
    pr_info("Hello from timer!\n");
    ++prints;
    ktime_t ktime = ktime_set(timeout_sec, timeout_nsec);
	hrtimer_forward_now(timer, ktime);

	return HRTIMER_RESTART;
}

static ssize_t timeout_sec_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%lld\n", timeout_sec);
}

static ssize_t timeout_sec_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int value;
    if (sscanf(buf, "%d", &value) != 1)
        return -EINVAL;

    timeout_sec = value;
    ktime_t ktime = ktime_set(TIMEOUT_SEC, TIMEOUT_NSEC);
    hrtimer_start(&hr_timer, ktime, HRTIMER_MODE_REL);
    return count;
}

static ssize_t procfile_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    if (*offset > 0)
        return 0;

    char tmpbuf[256];
    sprintf(tmpbuf, "%d timeouts so far\n", prints);
    int tmplen = strlen(tmpbuf);

    int error_count = copy_to_user(buffer, tmpbuf, tmplen);
    if (error_count != 0) {
        pr_alert("Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }

    *offset = tmplen;
    return tmplen;
}

static int timeout_get(char *buffer, const struct kernel_param *kp)
{
    return sprintf(buffer, "%lu\n", timeout_nsec);
}

static int timeout_set(const char *buffer, const struct kernel_param *kp)
{
    unsigned long value;
    if (sscanf(buffer, "%lu", &value) != 1)
        return -EINVAL;

    timeout_nsec = value;
    ktime_t ktime = ktime_set(timeout_sec, timeout_nsec);
    hrtimer_start(&hr_timer, ktime, HRTIMER_MODE_REL);
    return 0;
}

module_init(timerdrv_init);
module_exit(timerdrv_exit);
module_param_cb(timeout_nsec, &timeout_nsec_param_ops, NULL, 0644);
