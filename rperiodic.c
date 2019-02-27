#include <linux/version.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/time.h>

#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>

static struct hrtimer timer;
static unsigned int count;

static struct hrtimer htimer;
static ktime_t kt_periode;

static enum hrtimer_restart timer_function(struct hrtimer * timer);

static void timer_init(void)
{
	kt_periode = ktime_set(0, 104167); //seconds,nanoseconds
	hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
	htimer.function = timer_function;
	hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
}

static void timer_cleanup(void)
{
	hrtimer_cancel(& htimer);
}

static enum hrtimer_restart timer_function(struct hrtimer * timer)
{
	/* @Do your work here. */

	count++;
	hrtimer_forward_now(timer, kt_periode);

	return HRTIMER_RESTART;
}

enum hrtimer_restart do_timer(struct hrtimer *handle)
{
	count++;
	return HRTIMER_RESTART;
}

static int __init hrtimer_test_init(void)
{
	unsigned int resolution = hrtimer_resolution;

	timer_init();
	printk(KERN_ERR "resolution : %u secs\n", resolution);
	return 0;
}

static void __exit hrtimer_test_exit(void)
{
	int ret;

	ret = hrtimer_cancel(&htimer);
	printk(KERN_ERR "Cancelling hrtimer: %d, count: %u\n", ret, count);
	return ;
}

module_init(hrtimer_test_init);
module_exit(hrtimer_test_exit);
MODULE_AUTHOR("hrtimer test for demo only");
MODULE_DESCRIPTION("hrtimer resolution");
MODULE_LICENSE("GPL");
