// SPDX-License-Identifier: GPL-2.0
// author: Alexander Seifarth
//
// module executing kernel timer statistics tests

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include "lkm_timer_stats_ioctl.h"

#include <asm/barrier.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/timer.h>

MODULE_AUTHOR("Alexander Seifarth");
MODULE_DESCRIPTION("Kernel module for kernel timer statistic tests");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

#define MODULE_NAME "lkm_timer_stats"

static int lkm_timer_stats_open(struct inode *inode, struct file *filep);
static long lkm_timer_stats_ioctl(struct file *inode, unsigned int cmd, unsigned long arg);
static loff_t lkm_timer_stats_no_seek(struct file *, loff_t, int);
static int start_timer_stats_test(struct lkm_timer_stats_run __user *arg);
static void timer_fnc(struct timer_list *timer);
static int start_hrtimer_stats_test(struct lkm_timer_stats_run __user *arg);
static enum hrtimer_restart hrtimer_fnc(struct hrtimer *hrtimer);

struct lkm_timer_stats_tmr {
	unsigned int samples_count;
	unsigned int interval_us;
	struct timer_list tmr;
	unsigned int sample;
	u64 last_start;
	struct task_struct *task;
	char done_flag;
	u64 *measurements;
};

struct lkm_timer_stats_hrtmr {
	unsigned int samples_count;
	unsigned int interval_us;
	struct hrtimer tmr;
	unsigned int sample;
	u64 last_start;
	struct task_struct *task;
	char done_flag;
	u64 *measurements;
};

struct lkm_timer_stats_ctxt {
	const char * const name;	/* name of the module */
	int major;	/* device major number */
	const struct file_operations fops;
};

static struct lkm_timer_stats_ctxt ctxt = {
	.name = MODULE_NAME,
	.major = 0,
	.fops = {
		.open = lkm_timer_stats_open
	},
};

/* ----------------------------------------------------------------------------
 * module init/exit
 */
static int __init lkm_timer_stats_init(void)
{
	int res;

	res = register_chrdev(0, ctxt.name, &ctxt.fops);
	if (res < 0) {
		pr_warn("failed to register char device driver, status: %d\n",
			res);
		return res;
	}
	ctxt.major = res;
	pr_info("registered chdev with major = %d\n", ctxt.major);
	return 0;
}

static void __exit lkm_timer_stats_exit(void)
{
	unregister_chrdev(ctxt.major, ctxt.name);
	pr_info("removed\n");
}

module_init(lkm_timer_stats_init);
module_exit(lkm_timer_stats_exit);

/* ----------------------------------------------------------------------------
 * device node generic operations (without ioctl
 */
static const struct file_operations timer_stats_ioctl_fops = {
	.unlocked_ioctl = &lkm_timer_stats_ioctl,
	.llseek = lkm_timer_stats_no_seek,
};

static int lkm_timer_stats_open(struct inode *inode, struct file *filep)
{
	pr_info("device file opened with minor: %d\n", iminor(inode));

	switch (iminor(inode)) {
	case 0:
		filep->f_op = &timer_stats_ioctl_fops;
		break;
	default:
		return -ENXIO;
	}
	return 0;
}

/* ----------------------------------------------------------------------------
 * ioctl for timer stats
 */
static long lkm_timer_stats_ioctl(struct file *inode,
				  unsigned int cmd,
				  unsigned long arg)
{
	int retval;

	if (_IOC_TYPE(cmd) != LKM_TIMER_STATS_IOCTL_MAGIC) {
		pr_warn("invalid ioctl magic\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case LKM_TIMER_STATS_IOCTL_RUN:
		retval = start_timer_stats_test((struct lkm_timer_stats_run __user *)arg);
		break;
	case LKM_TIMER_STATS_IOCTL_RUN_HR:
		retval = start_hrtimer_stats_test((struct lkm_timer_stats_run __user *)arg);
		break;
	default:
		retval = -ENOTTY;
		break;
	}
	return retval;
}

static loff_t lkm_timer_stats_no_seek(struct file *filep, loff_t offs, int v)
{
	return -ESPIPE;
}


/* ----------------------------------------------------------------------------
 * hr timer stats execution
 */
static int start_hrtimer_stats_test(struct lkm_timer_stats_run __user *arg)
{

	int ret = 0;
	struct lkm_timer_stats_hrtmr *cmd;
	struct lkm_timer_stats_run run_arg;

	if (copy_from_user(&run_arg, arg, sizeof(run_arg))) {
		pr_warn("copy from user failed\n");
		return -ENOTTY;
	}

	if (run_arg.samples_count == 0 ||
		run_arg.samples_count > LKM_TIMER_STATS_MAX_SAMPLES ||
		run_arg.interval_us == 0 ||
		run_arg.measurements == NULL ||
		run_arg.measurements_len < run_arg.samples_count * sizeof(u64)) {
		pr_warn("invalid parameters\n");
		return -EINVAL;
	}

	/* there is no need for physically contigous memory, so we
	 * can use the virtual memory allocation
	 */
	cmd = kzalloc(sizeof(struct lkm_timer_stats_hrtmr), GFP_KERNEL);
	if (IS_ERR(cmd)) {

		ret = PTR_ERR(cmd);
		goto err;
	}

	cmd->measurements = kcalloc(run_arg.samples_count, sizeof(u64), GFP_KERNEL);
	if (IS_ERR(cmd->measurements)) {
		ret = PTR_ERR(cmd);
		goto cleanup;
	}

	cmd->samples_count = run_arg.samples_count;
	cmd->interval_us = run_arg.interval_us;
	cmd->sample = 0;
	cmd->task = current;
	cmd->done_flag = 0;
	hrtimer_init(&cmd->tmr, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cmd->tmr.function = &hrtimer_fnc;

	/* here we have to handle race condition between the timer callback
	 * and schedule().
	 * - first set current task to suspend (TASK_INTERRUPTIBLE)
	 * - then start timer (add_timer)
	 * - should the timer callback(s) finish before we call 'schedule' it will
	 *   set the task's state back to TASK_RUNNING and `schedule` will not remove
	 *   it from the run queue, else
	 * - schedule will remove the task from the run queue (put it really to sleep)
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	cmd->last_start = ktime_get_real_ns();
	hrtimer_start(&cmd->tmr, cmd->interval_us * NSEC_PER_USEC, HRTIMER_MODE_REL);
	schedule();

	__set_current_state(TASK_RUNNING); /* is it really required here? */
	if (signal_pending(current) || cmd->done_flag == 0) {
		ret = -EINTR;
		goto cleanup2;
	}

	if (copy_to_user(run_arg.measurements, cmd->measurements, cmd->samples_count * sizeof(u64))) {
		pr_warn("copy to user failed\n");
		ret = -EFAULT;
		goto cleanup2;
	}

cleanup2:
	hrtimer_cancel(&cmd->tmr);
	kfree(cmd->measurements);
cleanup:
	kfree(cmd);
err:
	return ret;
}

static enum hrtimer_restart hrtimer_fnc(struct hrtimer *hrtimer)
{
	struct lkm_timer_stats_hrtmr *cmd = container_of(hrtimer, typeof(*cmd), tmr);
	u64 real_intv = ktime_get_real_ns() - cmd->last_start;

	BUG_ON(cmd->samples_count <= cmd->sample);
	cmd->measurements[cmd->sample] = real_intv;
	pr_debug("real (hr) time interval #%d: %lld\n", cmd->sample, real_intv);

	if (cmd->samples_count > ++(cmd->sample)) {
		hrtimer_forward_now(&cmd->tmr, cmd->interval_us * NSEC_PER_USEC);
		cmd->last_start = ktime_get_real_ns();
		return HRTIMER_RESTART;
	}

	cmd->done_flag = 1;
	wmb(); /* the done_flag must be written before we wake up the task */
	wake_up_process(cmd->task);
	return HRTIMER_NORESTART;
}

/* ----------------------------------------------------------------------------
 * timer stats execution
 */
static int start_timer_stats_test(struct lkm_timer_stats_run __user *arg)
{
	int ret = 0;
	struct lkm_timer_stats_tmr *cmd;
	struct lkm_timer_stats_run run_arg;

	if (copy_from_user(&run_arg, arg, sizeof(run_arg))) {
		pr_warn("copy from user failed\n");
		return -ENOTTY;
	}

	if (run_arg.samples_count == 0 ||
		run_arg.samples_count > LKM_TIMER_STATS_MAX_SAMPLES ||
		run_arg.interval_us == 0 ||
		run_arg.measurements == NULL ||
		run_arg.measurements_len < run_arg.samples_count * sizeof(u64)) {
		pr_warn("invalid parameters\n");
		return -EINVAL;
	}

	/* there is no need for physically contigous memory, so we
	 * can use the virtual memory allocation
	 */
	cmd = kzalloc(sizeof(struct lkm_timer_stats_tmr), GFP_KERNEL);
	if (IS_ERR(cmd)) {

		ret = PTR_ERR(cmd);
		goto err;
	}

	cmd->measurements = kcalloc(run_arg.samples_count, sizeof(u64), GFP_KERNEL);
	if (IS_ERR(cmd->measurements)) {
		ret = PTR_ERR(cmd);
		goto cleanup;
	}

	cmd->samples_count = run_arg.samples_count;
	cmd->interval_us = run_arg.interval_us;
	cmd->sample = 0;
	cmd->tmr.expires = jiffies + usecs_to_jiffies(cmd->interval_us);
	cmd->tmr.flags = 0;
	cmd->task = current;
	cmd->done_flag = 0;
	timer_setup(&cmd->tmr, timer_fnc, 0);

	/* here we have to handle race condition between the timer callback
	 * and schedule().
	 * - first set current task to suspend (TASK_INTERRUPTIBLE)
	 * - then start timer (add_timer)
	 * - should the timer callback(s) finish before we call 'schedule' it will
	 *   set the task's state back to TASK_RUNNING and `schedule` will not remove
	 *   it from the run queue, else
	 * - schedule will remove the task from the run queue (put it really to sleep)
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	cmd->last_start = ktime_get_real_ns();
	add_timer(&cmd->tmr);
	schedule();

	__set_current_state(TASK_RUNNING); /* is it really required here? */
	if (signal_pending(current) || cmd->done_flag == 0) {
		ret = -EINTR;
		goto cleanup1;
	}

	if (copy_to_user(run_arg.measurements, cmd->measurements, cmd->samples_count * sizeof(u64))) {
		pr_warn("copy to user failed\n");
		ret = -EFAULT;
		goto cleanup1;
	}

cleanup1:
	kfree(cmd->measurements);
cleanup:
	del_timer(&cmd->tmr);
	kfree(cmd);
err:
	return ret;
}

static void timer_fnc(struct timer_list *timer)
{
	struct lkm_timer_stats_tmr *cmd = from_timer(cmd, timer, tmr);
	u64 real_intv = ktime_get_real_ns() - cmd->last_start;

	BUG_ON(cmd->samples_count <= cmd->sample);
	cmd->measurements[cmd->sample] = real_intv;
	pr_debug("real time interval #%d: %lld\n", cmd->sample, real_intv);

	if (cmd->samples_count > ++(cmd->sample)) {
		cmd->last_start = ktime_get_real_ns();
		mod_timer(&cmd->tmr, jiffies + usecs_to_jiffies(cmd->interval_us));
	} else {
		cmd->done_flag = 1;
		wmb(); /* the done_flag must be written before we wake up the task */
		wake_up_process(cmd->task);
	}
}

