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

#include "spp.h"

static enum hrtimer_restart timer_handler(struct hrtimer * timer);

struct adc_sample {
	u8 buf[24];
};

#define ADC_MAX_SAMPLES 64
static struct adc_sample samples[ADC_MAX_SAMPLES];

struct spp_periodic {
	struct hrtimer timer;
	ktime_t period;
	unsigned int nr_ticks;

	DECLARE_KFIFO_PTR(rx_fifo, struct adc_sample *);
	DECLARE_KFIFO_PTR(free_fifo, struct adc_sample *);
};

#define to_spp_periodic(ptr) container_of(ptr, struct spp_periodic, timer)

static struct adc_sample *adc_buf_get(struct spp_periodic *sp)
{
	struct adc_sample *r;
	unsigned int n;

	n = kfifo_get(&sp->free_fifo, &r);
	printk(KERN_INFO "spp: adc_get: %u, %p\n", n, r);
	return r;
}

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
	struct adc_sample *r;

	s = to_spp_periodic(timer);
	s->nr_ticks++;

	r = adc_buf_get(s);

	hrtimer_forward_now(timer, s->period);
	printk(KERN_INFO "in irq: %lu, %p\n", in_irq(), r);

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
	spp_rx_fifo_init(&spp);
	spp_free_fifo_init(&spp);
	filp->private_data = &spp;
	return 0;
}

static int spp_close(struct inode *inode, struct file *filp)
{
	struct spp_periodic *spp = filp->private_data;
	struct adc_sample *smpls[120];
	unsigned int len;

	memset(smpls, 0, sizeof smpls);
	len = kfifo_out(&spp->free_fifo, smpls, 3);
	printk(KERN_INFO "spp: close: sz: %zu, len: %u, ptr: %p, ptr: %p\n",
	       sizeof(smpls), len, smpls[2], smpls[3]);
	printk(KERN_INFO "spp: rx_len: %u, free_len: %u\n",
	       kfifo_len(&spp->free_fifo), kfifo_len(&spp->free_fifo));
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

	err = kfifo_alloc(&spp.rx_fifo, ADC_MAX_SAMPLES, GFP_KERNEL);
	if (err)
		goto err_rx_fifo;

	err = kfifo_alloc(&spp.free_fifo, ADC_MAX_SAMPLES, GFP_KERNEL);
	if (err)
		goto err_free_fifo;

	err = misc_register(&perdev);
	if (err)
		goto err_register;

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
