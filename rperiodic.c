#include <linux/version.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/time.h>

#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>

static unsigned int count;

static struct hrtimer htimer;
static ktime_t kt_period;

static enum hrtimer_restart timer_function(struct hrtimer * timer);

static void timer_init(void)
{
	kt_period = ktime_set(0, 22675); //seconds,nanoseconds
	hrtimer_init (&htimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	htimer.function = timer_function;
	hrtimer_start(&htimer, kt_period, HRTIMER_MODE_REL);
}

static enum hrtimer_restart timer_function(struct hrtimer * timer)
{
	count++;
	hrtimer_forward_now(timer, kt_period);

	return HRTIMER_RESTART;
}

static ssize_t spp_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t spp_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	return -EBADF;
}

static long spp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static int spp_open(struct inode *ino, struct file *filp)
{
	return 0;
}

static int spp_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations sp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= spp_read,
	.write		= spp_write,
	.unlocked_ioctl	= spp_ioctl,
	.open		= spp_open,
	.release	= spp_close,
};

static struct miscdevice perdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "spiperiodic",
	.fops = &sp_fops,
};

static int __init hrtimer_test_init(void)
{
	int err;
	unsigned int resolution = hrtimer_resolution;

	timer_init();
	printk(KERN_INFO "resolution : %u secs\n", resolution);

	err = misc_register(&perdev);
	if (err) {
		printk(KERN_ERR "error registering misc device\n");
		return -EINVAL;
	}

	return 0;
}

static void __exit hrtimer_test_exit(void)
{
	int ret;

	ret = hrtimer_cancel(&htimer);
	printk(KERN_INFO "Cancelling hrtimer: %d, count: %u\n", ret, count);

	misc_deregister(&perdev);

	return ;
}

module_init(hrtimer_test_init);
module_exit(hrtimer_test_exit);
MODULE_AUTHOR("hrtimer test for demo only");
MODULE_DESCRIPTION("hrtimer resolution");
MODULE_LICENSE("GPL");
