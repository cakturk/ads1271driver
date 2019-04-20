#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
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
#include <linux/spi/spi.h>
#include <linux/atomic.h>

#include "spp.h"

struct spp_periodic;

static enum hrtimer_restart timer_handler(struct hrtimer * timer);

static inline int
spp_async_tx(struct spp_periodic *spp);

struct adc_sample {
	u32 sample_nr;
	u8 buf[24];
};

#define to_adc_sample(ptr) container_of(ptr, struct adc_sample, buf[0])

#define ADC_MAX_SAMPLES 64
static struct adc_sample samples[ADC_MAX_SAMPLES];

struct spp_periodic {
	struct hrtimer timer;
	ktime_t period;

	struct spi_device *spi;
	struct spi_transfer strans;
	struct spi_message smsg;

	atomic_t pending_transfers;

	struct spp_stats {
		u32 nr_ticks;
		u32 nr_overruns;
		u32 nr_uncompleted;
		u32 nr_failed_tx;
	} stats;

	DECLARE_KFIFO_PTR(rx_fifo, struct adc_sample *);
	DECLARE_KFIFO_PTR(free_fifo, struct adc_sample *);

	wait_queue_head_t waitq;
	wait_queue_head_t wait_for_finish;
};

#define to_spp_periodic(ptr) container_of(ptr, struct spp_periodic, timer)

static void stats_display(struct spp_stats *s)
{
	pr_info("spp: stats: ticks: %u, overruns: %u, uncompleted: %u, failed_tx: %u\n",
		s->nr_ticks, s->nr_overruns, s->nr_uncompleted, s->nr_failed_tx);
}

static void spp_free_fifo_init(struct spp_periodic *sp)
{
	struct adc_sample *as = NULL;
	int i, rv;

	for (i = 0; i < ARRAY_SIZE(samples); i++) {
		const struct adc_sample *a1 = &samples[i];
		rv = kfifo_put(&sp->free_fifo, &a1);
		/* printk(KERN_INFO "spp: kfifo_put: %p, %d, %d, len: %d\n", */
		/*        &samples[i], rv, kfifo_avail(&sp->free_fifo), kfifo_len(&sp->free_fifo)); */
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
	int rv;

	s = to_spp_periodic(timer);

	if (s->stats.nr_ticks > 8 && 1) {
		printk(KERN_WARNING "spp: reached ratelimit stopping: %d\n",
		       atomic_read(&s->pending_transfers));
		wake_up_interruptible(&s->waitq);
		return HRTIMER_NORESTART;
	}

	rv = spp_async_tx(s);
	if (rv)
		goto setup_timer;
setup_timer:
	hrtimer_forward_now(timer, s->period);

	return HRTIMER_RESTART;
}

static ssize_t spp_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos)
{
	static struct adc_sample *smp[ADC_MAX_SAMPLES];
	struct spp_periodic *s = filp->private_data;
	size_t nr_sample;
	unsigned int i, n;
	ssize_t ret = 0;

	if (count < sizeof(struct adc_sample))
		return -EINVAL;

	nr_sample = count / sizeof(struct adc_sample);
	nr_sample = min(nr_sample, ARRAY_SIZE(smp));
retry:
	n = kfifo_out(&s->rx_fifo, smp, nr_sample);
	if (!n) {
		if (!atomic_read(&s->pending_transfers))
			return 0;

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(s->waitq,
					       kfifo_len(&s->rx_fifo) != 0);
		if (ret)
			return ret;
		goto retry;
	}

	pr_debug("spp: read syscall: cpd: %u\n", n);

	for (i = 0; i < n; i++) {
		struct adc_sample *as = smp[i];

		if (copy_to_user(buf, as, sizeof(*as))) {
			ret = -EFAULT;
			break;
		}
		buf += sizeof(*as);
		ret += sizeof(*as);
	}

	if (kfifo_in(&s->free_fifo, (const struct adc_sample **)&smp[0], n) != n)
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
		if ((ret = atomic_cmpxchg(&sp->pending_transfers, 0, 1)) != 0)
			return -EINVAL;
		hrtimer_start(&sp->timer, sp->period, HRTIMER_MODE_REL);
		break;
	case SPPIOC_STOP:
		if (!atomic_add_unless(&sp->pending_transfers, -1, 0))
			return -EINVAL;
		ret = hrtimer_cancel(&sp->timer);
		if (!ret)
			printk(KERN_INFO "timer was not active\n");
		break;
	case SPPIOC_SPARAMS:
		if (copy_from_user(&cnf, from, sizeof(cnf)))
			return -EFAULT;
		pr_info("spp: cnf params: %lld secs, %llu nsecs\n",
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

static int spp_dev_init(struct spi_device *spi)
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
	init_waitqueue_head(&spp.wait_for_finish);
	atomic_set(&spp.pending_transfers, 0);
	spp.stats.nr_ticks = 0;
	spp.spi = spi;
	spp_rx_fifo_init(&spp);
	spp_free_fifo_init(&spp);
	hrtimer_init(&spp.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	spp.timer.function = timer_handler;

	return 0;

err_register:
	printk(KERN_ERR "spp: error registering misc device\n");
	kfifo_free(&spp.free_fifo);
err_free_fifo:
	kfifo_free(&spp.rx_fifo);
err_rx_fifo:
	return err;
}

static void spp_dev_exit(void)
{
	long timout;
	int ret;

	ret = hrtimer_cancel(&spp.timer);
	pr_info("spp: cancelling hrtimer: %d, count: %u\n",
		ret, spp.stats.nr_ticks);

	if (atomic_add_unless(&spp.pending_transfers, -1, 0)) {
		timout = wait_event_timeout(spp.wait_for_finish,
					    !atomic_read(&spp.pending_transfers),
					    msecs_to_jiffies(500));
		pr_info("spp: wait_event: %ld, pending tx: %d\n", timout,
			atomic_read(&spp.pending_transfers));
	}
	pr_info("spp: last info: pending tx: %d, rx len: %u\n",
		atomic_read(&spp.pending_transfers), kfifo_len(&spp.rx_fifo));
	stats_display(&spp.stats);
	misc_deregister(&perdev);
	kfifo_free(&spp.rx_fifo);
	kfifo_free(&spp.free_fifo);
}

/* #define __devexit_p(x) x */
/* #define __devinit */
/* #define __devexit */

#define SPP_SPI_BUS 2

static inline int spp_spi_setup(struct spi_device *spi,
				u32 hz, u8 bpw, u16 mode)
{
	spi->max_speed_hz = hz;
	spi->bits_per_word = bpw;
	spi->mode = mode;

	return spi_setup(spi);
}

static void spp_spi_complete(void *ctx)
{
	struct spi_transfer *t = ctx;
	struct spp_periodic *sp = container_of(t, struct spp_periodic, strans);
	const struct adc_sample *sample = to_adc_sample(t->rx_buf);
	int status = sp->smsg.status;
	bool exiting;

	/* implies a memory barrier */
	exiting = atomic_dec_and_test(&sp->pending_transfers);

	/* in case of error, return buffer to free list */
	if (unlikely(status)) {
		printk(KERN_ERR "spp: spp_spi_complete: spi tx error\n");
		sp->stats.nr_uncompleted++;
		kfifo_put(&sp->free_fifo, &sample);
		goto wake_up;
	}
	if (unlikely(!kfifo_put(&sp->rx_fifo, &sample)))
		printk(KERN_ERR "spp: spp_spi_complete: rx_fifo put error\n");
	/* print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, */
	/* 	       16, 1, s->buf, 24, 0); */
wake_up:
	wake_up_interruptible(&sp->waitq);
	if (exiting)
		wake_up(&sp->wait_for_finish);
}

static inline int
spp_async_tx(struct spp_periodic *spp)
{
	struct adc_sample       *r;
	struct spi_message      *m = &spp->smsg;
	struct spi_transfer     *t = &spp->strans;
	unsigned		n;
	int			ret;
	u32			sample_seq;

	sample_seq = spp->stats.nr_ticks++;

	n = kfifo_peek(&spp->free_fifo, &r);
	if (!n) {
		pr_debug("spp: fifo overrun\n");
		spp->stats.nr_overruns++;
		return -1;
	}
	if (!r) {
		printk(KERN_INFO "spp: fifo is null\n");
		return -1;
	}

	ret = atomic_cmpxchg(&spp->pending_transfers, 1, 2);
	if (ret > 1) {
		pr_debug("spp: there are pending spi requests: %d, curr: %d, %u\n",
			 ret, atomic_read(&spp->pending_transfers), sample_seq);
		spp->stats.nr_uncompleted++;
		return -1;
	}
	if (!ret) {
		pr_warn("spp: not running\n");
		return -1;
	}

	spi_message_init(m);
	spi_message_add_tail(t, m);

	m->complete = spp_spi_complete;
	m->context = t;
	t->tx_buf = NULL;
	t->rx_buf = r->buf;
	t->len = 24;

	ret = spi_async(spp->spi, m);
	if (ret) {
		printk(KERN_WARNING "spp: spi request returned: %d\n", ret);
		spp->stats.nr_failed_tx++;
		goto out_err;
	}
	r->sample_nr = sample_seq;
	kfifo_skip(&spp->free_fifo);
	return ret;

out_err:
	if (atomic_dec_and_test(&spp->pending_transfers))
		wake_up(&spp->wait_for_finish);
	return ret;
}

static inline ssize_t
spp_sync_tx(struct spi_device *spi, u8 *rx, size_t len)
{
	static u8 tx[24];

	struct spi_transfer     t = {
		.tx_buf         = tx,
		.rx_buf         = rx,
		.len            = len,
	};
	struct spi_message      m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

static int __devinit spp_spi_probe(struct spi_device *spi)
{
	int rv;

	if (spi->master->bus_num != SPP_SPI_BUS)
		return -ENODEV;

	printk(KERN_INFO "spp: spi_probe: bus-cs: %d-%d, %u\n",
	       spi->master->bus_num, spi->chip_select, spi->max_speed_hz);

	if ((rv = spp_spi_setup(spi, 24000000, 8, SPI_MODE_1)))
		return rv;

	return spp_dev_init(spi);
}

static int __devexit spp_spi_remove(struct spi_device *spi)
{
	printk(KERN_INFO "spp: spi_remove\n");
	spp_dev_exit();

	return 0;
}

static struct spi_driver spp_spi_driver = {
	.driver = {
		.name =         "spidev",
		.owner =        THIS_MODULE,
	},
	.probe =        spp_spi_probe,
	.remove =       __devexit_p(spp_spi_remove),
};

static int __init spp_init(void)
{
	return spi_register_driver(&spp_spi_driver);
}

static void __exit spp_exit(void)
{
	spi_unregister_driver(&spp_spi_driver);
}

module_init(spp_init);
module_exit(spp_exit);
MODULE_AUTHOR("Cihangir Akturk");
MODULE_DESCRIPTION("TI ADS1271 ADC daisy chain driver");
MODULE_LICENSE("GPL");
