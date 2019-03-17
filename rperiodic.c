#include <linux/version.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

#include "spp.h"

static enum hrtimer_restart timer_handler(struct hrtimer * timer);

struct spp_periodic {
	struct hrtimer timer;
	ktime_t period;
	unsigned int nr_ticks;
};

#define to_spp_periodic(ptr) container_of(ptr, struct spp_periodic, timer)

/* static unsigned int count; */
/* static void timer_init(void) */
/* { */
/* 	kt_period = ktime_set(0, 22675); //seconds,nanoseconds */
/* 	hrtimer_init (&htimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL); */
/* 	htimer.function = timer_handler; */
/* 	hrtimer_start(&htimer, kt_period, HRTIMER_MODE_REL); */
/* } */


static enum hrtimer_restart timer_handler(struct hrtimer *timer)
{
	struct spp_periodic *s;

	s = to_spp_periodic(timer);
	hrtimer_forward_now(timer, s->period);
	s->nr_ticks++;

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
	const void __user *from;
	struct spp_conf cnf;
	struct spp_periodic *sp;
	int ret;

	sp = filp->private_data;
	from = (const void __user *)arg;

	switch (cmd) {
	case SPPIOC_START:
		hrtimer_start(&sp->timer, sp->period, HRTIMER_MODE_REL);
		break;
	case SPPIOC_STOP:
		ret = hrtimer_cancel(&sp->timer);
		if (!ret)
			printk(KERN_INFO "timer was not active\n");
		break;
	case SPPIOC_SPARAMS:
		if (copy_from_user(&cnf, from, sizeof(cnf)))
			return -EFAULT;
		printk(KERN_INFO "cnf params: %lld secs, %llu nsecs\n",
		       cnf.secs, cnf.nsecs);
		sp->period = ktime_set(cnf.secs, cnf.nsecs);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}


static struct spp_periodic spp;

static int spp_open(struct inode *ino, struct file *filp)
{
	filp->private_data = &spp;
	return 0;
}

static int spp_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations spp_fops = {
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
	.fops = &spp_fops,
};

static int __init spp_init(void)
{
	int err;

	err = misc_register(&perdev);
	if (err) {
		printk(KERN_ERR "error registering misc device\n");
		return -EINVAL;
	}

	hrtimer_init(&spp.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	spp.timer.function = timer_handler;

	printk(KERN_INFO "resolution : %u secs\n", hrtimer_resolution);

	return 0;
}

static void __exit spp_exit(void)
{
	int ret;

	ret = hrtimer_cancel(&spp.timer);
	printk(KERN_INFO "Cancelling hrtimer: %d, count: %u\n",
	       ret, spp.nr_ticks);

	misc_deregister(&perdev);

	return;
}

module_init(spp_init);
module_exit(spp_exit);
MODULE_AUTHOR("Cihangir Akturk");
MODULE_DESCRIPTION("TI ADS1271 daisy chain driver");
MODULE_LICENSE("GPL");
