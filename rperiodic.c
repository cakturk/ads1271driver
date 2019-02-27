#include <linux/version.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/time.h>

#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>

static unsigned int count;

static struct hrtimer htimer;
static ktime_t kt_periode;

static enum hrtimer_restart timer_function(struct hrtimer * timer);
static enum hrtimer_restart timer2_function(struct hrtimer * timer);

static struct hrtimer htimer2;

static void timer_init(void)
{
	kt_periode = ktime_set(0, 104167); //seconds,nanoseconds
	hrtimer_init (&htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
	htimer.function = timer_function;
	hrtimer_start(&htimer, kt_periode, HRTIMER_MODE_REL);
}

static void timer2_init(void)
{
	ktime_t t = ktime_set(1, 0);
	hrtimer_init(&htimer2, CLOCK_REALTIME, HRTIMER_MODE_REL);
	htimer.function = timer2_function;
	hrtimer_start(&htimer2, t, HRTIMER_MODE_REL);
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

static enum hrtimer_restart timer2_function(struct hrtimer * timer)
{
	/* @Do your work here. */
	printk(KERN_INFO"1 second elapsed!\n");
	timer_cleanup();
	printk(KERN_INFO"1 second elapsed stopped sampler!\n");

	return HRTIMER_NORESTART;
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
	/* timer2_init(); */
	printk(KERN_INFO "resolution : %u secs\n", resolution);
	return 0;
}

static void __exit hrtimer_test_exit(void)
{
	int ret;

	/* ret = hrtimer_cancel(&htimer2); */
	/* printk(KERN_INFO "Cancelling main timer: %d\n", ret); */

	ret = hrtimer_cancel(&htimer);
	printk(KERN_INFO "Cancelling hrtimer: %d, count: %u\n", ret, count);

	return ;
}

module_init(hrtimer_test_init);
module_exit(hrtimer_test_exit);
MODULE_AUTHOR("hrtimer test for demo only");
MODULE_DESCRIPTION("hrtimer resolution");
MODULE_LICENSE("GPL");
