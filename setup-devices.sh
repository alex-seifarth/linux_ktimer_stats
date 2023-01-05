#!/bin/env bash

# creates the device node for our driver
DRIVER_NAME="lkm_timer_stats"
DEVICE_NODE=/dev/lkm_timer_stats

MAJOR=$(grep "${DRIVER_NAME}" /proc/devices | awk '{print $1}')
[ -z "${MAJOR}" ] && {
	echo "Failed to retrieve major device number for driver '${DRIVER_NAME}'"
	echo "... aborting"
	exit 1
}

echo "major device number: ${MAJOR}"
sudo rm -f "${DEVICE_NODE}"
sudo mknod "${DEVICE_NODE}" c ${MAJOR} 0
sudo chmod 0666 "${DEVICE_NODE}"
ls -l "${DEVICE_NODE}"
exit 0

