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
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/wait.h>

#include "spp.h"

static enum hrtimer_restart timer_handler(struct hrtimer * timer);

struct adc_sample {
	u8 buf[24];
};

#define ADC_MAX_SAMPLES 8
static struct adc_sample samples[ADC_MAX_SAMPLES];

struct spp_periodic {
	struct hrtimer timer;
	ktime_t period;
	unsigned int nr_ticks;

	DECLARE_KFIFO_PTR(rx_fifo, struct adc_sample *);
	DECLARE_KFIFO_PTR(free_fifo, struct adc_sample *);

	wait_queue_head_t waitq;
};

#define to_spp_periodic(ptr) container_of(ptr, struct spp_periodic, timer)

static void spp_free_fifo_init(struct spp_periodic *sp)
{
	struct adc_sample *as = NULL;
	int i, rv;

	for (i = 0; i < ARRAY_SIZE(samples); i++) {
		rv = kfifo_put(&sp->free_fifo, &samples[i]);
		printk(KERN_INFO "spp: kfifo_put: %p, %d, %d, len: %d\n",
		       &samples[i], rv, kfifo_avail(&sp->free_fifo), kfifo_len(&sp->free_fifo));
	}

	rv = kfifo_peek(&sp->free_fifo, &as);
	printk(KERN_INFO "spp: peek: %p, rv: %d, size: %zu, len: %zu\n",
	       as, rv, sizeof(sp->free_fifo), sizeof(samples[1]));
}

static inline void spp_rx_fifo_init(struct spp_periodic *sp)
{
	kfifo_reset(&sp->rx_fifo);
}

static enum hrtimer_restart timer_handler(struct hrtimer *timer)
{
	struct spp_periodic *s;
	struct adc_sample *r = NULL;
	unsigned int n;
	static unsigned counter = 1;

	s = to_spp_periodic(timer);
	s->nr_ticks++;

	printk(KERN_INFO "spp: free_fifo: avail: %u, len: %u\n",
	       kfifo_avail(&s->free_fifo), kfifo_len(&s->free_fifo));
	n = kfifo_get(&s->free_fifo, &r);
	if (n < 1) {
		printk(KERN_INFO "spp: fifo overrun\n");
		goto setup_timer;
	}
	printk(KERN_INFO "spp: putting sample: %u\n", counter);
	memset(r, counter, sizeof(*r));
	n = kfifo_put(&s->rx_fifo, r);
	wake_up_interruptible(&s->waitq);
setup_timer:
	counter++;
	hrtimer_forward_now(timer, s->period);

	return HRTIMER_RESTART;
}

static ssize_t spp_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos)
{
	static struct adc_sample *smp[ADC_MAX_SAMPLES];
	struct spp_periodic *s = filp->private_data;
	unsigned long nr_sample;
	unsigned int i, n;
	ssize_t ret = 0;

	if (count < sizeof(struct adc_sample))
		return -EINVAL;

	nr_sample = count / sizeof(struct adc_sample);
	nr_sample = min(nr_sample, ARRAY_SIZE(smp));
	n = kfifo_out(&s->rx_fifo, smp, nr_sample);
	if (!n)
		return -EAGAIN;

	printk(KERN_INFO "spp: read syscall: cpd: %u\n", n);

	for (i = 0; i < n; i++) {
		struct adc_sample *as = smp[i];

		if (copy_to_user(buf, as, sizeof(*as))) {
			ret = -EFAULT;
			break;
		}
		buf += sizeof(*as);
		ret += sizeof(*as);
	}

	if (kfifo_in(&s->free_fifo, smp, n) != n)
		printk(KERN_WARNING "spp: not enough room at free_fifo\n");

	return ret;
}

static unsigned int spp_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct spp_periodic *sp = filp->private_data;

	poll_wait(filp, &sp->waitq, wait);
	if (kfifo_len(&sp->rx_fifo))
		return POLLIN | POLLRDNORM;
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
	.poll		= spp_poll,
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

	err = kfifo_alloc(&spp.rx_fifo, ADC_MAX_SAMPLES, GFP_KERNEL);
	if (err)
		goto err_rx_fifo;

	err = kfifo_alloc(&spp.free_fifo, ADC_MAX_SAMPLES, GFP_KERNEL);
	if (err)
		goto err_free_fifo;

	err = misc_register(&perdev);
	if (err)
		goto err_register;

	init_waitqueue_head(&spp.waitq);
	spp_rx_fifo_init(&spp);
	spp_free_fifo_init(&spp);
	hrtimer_init(&spp.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	spp.timer.function = timer_handler;
	printk(KERN_INFO "resolution : %u secs\n", hrtimer_resolution);

	return 0;

err_register:
	printk(KERN_ERR "error registering misc device\n");
	kfifo_free(&spp.free_fifo);
err_free_fifo:
	kfifo_free(&spp.rx_fifo);
err_rx_fifo:
	return err;
}

static void __exit spp_exit(void)
{
	int ret;

	ret = hrtimer_cancel(&spp.timer);
	printk(KERN_INFO "Cancelling hrtimer: %d, count: %u\n",
	       ret, spp.nr_ticks);

	misc_deregister(&perdev);
	kfifo_free(&spp.rx_fifo);

	return;
}

module_init(spp_init);
module_exit(spp_exit);
MODULE_AUTHOR("Cihangir Akturk");
MODULE_DESCRIPTION("TI ADS1271 daisy chain driver");
MODULE_LICENSE("GPL");
