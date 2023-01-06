// SPDX-License-Identifier: GPL-2.0
// author: Alexander Seifarth
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>

#include "lkm_timer_stats_ioctl.h"

#define MAX_INTERVAL_US		30000000

int main(int argc, char *argv[])
{
	int fd, res, hres;

	if (argc != 5) {
		fprintf(stderr, "Usage %s <device file path> <interval in us> <number of samples> [hr|kt]\n",
			argv[0]);
		exit(EXIT_FAILURE);
	}

	int interval_us = atoi(argv[2]);

	if (interval_us <= 0 || interval_us > MAX_INTERVAL_US) {
		fprintf(stderr, "Invalid time interval in us: %s\n"
			"Must be 1..%d\n", argv[2], MAX_INTERVAL_US);
		exit(EXIT_FAILURE);
	}

	int samples_count = atoi(argv[3]);

	if (samples_count == 0 || samples_count > LKM_TIMER_STATS_MAX_SAMPLES) {
		fprintf(stderr, "Invalid number of sumples: %s\n"
			"Must be 1..%d\n", argv[3], LKM_TIMER_STATS_MAX_SAMPLES);
		exit(EXIT_FAILURE);
	}


	if (strncmp(argv[4], "hr", 2) == 0)
		hres = 1;
	else if (strncmp(argv[4], "kt", 2) == 0)
		hres = 0;
	else {

		fprintf(stderr, "Invalid timer type: %s\nMust be 'hr' or 'kt'.",
			argv[4]);
		exit(EXIT_FAILURE);
	}


	fd = open(argv[1], O_RDWR, 0);
	if (fd == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	u64 *measurements = calloc(samples_count, sizeof(u64));

	struct lkm_timer_stats_run cmd = {
		.samples_count = samples_count,
		.interval_us = interval_us,
		.measurements = measurements,
		.measurements_len = samples_count * sizeof(u64),
	};

	if (hres == 0)
		res = ioctl(fd, LKM_TIMER_STATS_IOCTL_RUN, &cmd);
	else
		res = ioctl(fd, LKM_TIMER_STATS_IOCTL_RUN_HR, &cmd);

	if (res == -1) {
		perror("ioctl LKM_TIMER_STATS_IOCTL failed");
		close(fd);
		exit(EXIT_FAILURE);
	}

	printf("%6.2f", interval_us * 1.f);
	for (int i = 0; i < samples_count; ++i)
		printf("\t%8.2f", measurements[i]/1000.);
	printf("\n");

	free(measurements);
	close(fd);
	return 0;
}
