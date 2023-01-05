/* SPDX-License-Identifier: GPL-2.0
 *
 * author: Alexander Seifarth
 */
#ifndef _LKM_TIMER_STATS_IOCTL_H_
#define _LKM_TIMER_STATS_IOCTL_H_

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/kernel.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
typedef uint64_t u64;
#define __user
#endif

#define LKM_TIMER_STATS_MAX_SAMPLES	1024

struct lkm_timer_stats_run {
	/* number of timer runs to do, 1..LKM_TIMER_STATS_MAX_SAMPLES */
	unsigned int samples_count;

	/* time interval in us (microseconds), 1.. */
	unsigned int interval_us;

	/* pointer to buffer for the measurements */
	u64 __user *measurements;

	/* length of measurements buffer */
	ssize_t measurements_len;
};

#define LKM_TIMER_STATS_IOCTL_MAGIC	0xa9

#define LKM_TIMER_STATS_IOCTL_RUN	_IOWR(LKM_TIMER_STATS_IOCTL_MAGIC, 1, struct lkm_timer_stats_run *)

#endif /* _LKM_TIMER_STATS_IOCTL_H_ */

