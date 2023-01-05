#!/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

DRIVER_NAME="lkm_timer_stats"
DEVICE_NODE=/dev/lkm_timer_stats
COUNT_SAMPLES=512
APP="${SCRIPT_DIR}/app"

if [ -e "${DEVICE_NODE}" ]; then
	echo "device file ${DEVICE_NODE} exists" >&2
	exit 1
fi

if ! sudo insmod ${DRIVER_NAME}.ko; then
	exit 1
fi

if [ ! -e "${APP}" ]; then
	echo "application not existing: ${APP}\n" >&2
	exit 1
fi


MAJOR=$(grep "${DRIVER_NAME}" /proc/devices | awk '{print $1}')
[ -z "${MAJOR}" ] && {
	echo "Failed to retrieve major device number for driver '${DRIVER_NAME}'" >&2
	echo "... aborting" >&2
	sudo rmmod ${DRIVER_NAME}.ko
	exit 1
}

echo "major device number: ${MAJOR}" >&2
sudo mknod "${DEVICE_NODE}" c ${MAJOR} 0
sudo chmod 0666 "${DEVICE_NODE}"
ls -l "${DEVICE_NODE}" >&2

for b in 1 10 100 1000 10000 100000
do
	for m in 1 2 5
	do
		iv=$(($b * $m))
		echo "Interval $iv us ..." >&2
		echo $(${APP} "${DEVICE_NODE}" "${iv}" "${COUNT_SAMPLES}")
	done
done

sudo rm -f "${DEVICE_NODE}"
sudo rmmod lkm_timer_stats





