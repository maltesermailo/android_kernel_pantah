#! /bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022, Google LLC. All rights reserved.
# Author: Isaac J. Manjarres <isaacmanjarres@google.com>

progname=${0##*/}

USAGE="USAGE: ${progname} [-s <serialno>] [-r|-l|-h]

This script can be used to derive information about the supplier and consumer
relationships between devices on a system, and by extension, their device
drivers.

By default, the script will create a dependency ordered list of drivers that
can be used as is to figure out what order drivers need to be ported to a
new kernel in.

Each driver is represented by the compatible string that it used
to match with a device on the system. The list is ordered such that for any
driver entry, that driver does not depend on drivers listed below it, but may
depend on drivers listed above it.

-s <serialno> specifies a device to connect to when multiple devices
are available, otherwise will default to one available or ANDROID_SERIAL
environment variable.

-h causes the script to emit this help text and exit.

One of the following options can be specified:

-r causes the list to be emitted in reverse order. This is useful when figuring
out what order drivers should be modularized in on a particular kernel. The
default ordering causes drivers without dependencies--leaf drivers--to be placed
at the end of the list. Since they do not have dependencies, they can safely be
removed from the kernel image and loaded as modules. As those drivers are
modularized, the number of dependencies in the kernel image for other drivers
reduces, until they become leaf drivers, and can also be modularized and so on.

-l causes the script to create a dependency ordered list of modules found on
the system. This list can be used as the optimal module loading order, since
it lists the supplier modules first, followed by their dependencies. Each module
is represented by its name."


DIR="$(dirname $(readlink -f $0))"
DEV_NEEDS_HOST_PATH="${DIR}/dev-needs.sh"

DEV_TMP_DIR="/data/local/tmp"
DEV_NEEDS_DEV_PATH="${DEV_TMP_DIR}/$(basename ${DEV_NEEDS_HOST_PATH})"

REVERSE_DRIVER_DEP_LIST=0
WANT_MOD_LOAD_ORDER=0

prepare_device() {
	adb wait-for-device root >/dev/null
	if [ $? -ne 0 ]; then
		echo "Failed to root device"
		return 1
	fi

	adb push "${DEV_NEEDS_HOST_PATH}" "${DEV_TMP_DIR}/" >/dev/null
	if [ $? -ne 0 ]; then
		echo "Failed to push the dev-needs.sh script to the device"
		return 1
	fi

	return 0
}

cleanup_device() {
	adb shell "rm ${DEV_NEEDS_DEV_PATH}"
}

remove_duplicate_list_entries () {
	local list_entries="${1}"
	local tmp_list_file=$(mktemp)

	echo -n "${list_entries}" > "${tmp_list_file}"

	# Remove duplicates--except for the first occurrence--while keeping
	# the list in the same order
	OUTPUT_LIST=$(cat -n "${tmp_list_file}" | sort -uk2 | sort -n | cut -f2-)
	rm "${tmp_list_file}"
}

get_dev_compat() {
	local dev=$1
	local compat_str=$(adb shell "cat '$dev'/of_node/compatible 2>/dev/null" | tr -d '\0')

	if [ -n "${compat_str}" ]; then
		OUTPUT_LIST+="${compat_str}"$'\n'
	fi
}

devs_to_compat_list() {
	local dev_list="${1}"

	for dev in ${dev_list[@]}; do
		get_dev_compat "${dev}"
	done
}

get_dev_module_name() {
	local dev=$1
	local module_path=$(adb shell "realpath '$1'/driver/module 2>/dev/null")

	if [ -n "${module_path}" ]; then
		OUTPUT_LIST+=$(basename "${module_path}")$'\n'
	fi
}

devs_to_module_list() {
	local dev_list="${1}"

	for dev in ${dev_list[@]}; do
		get_dev_module_name "${dev}"
	done
}

get_tsorted_dev_list() {
	local tsort_edges

	tsort_edges=$(adb shell ''${DEV_NEEDS_DEV_PATH}' -t\
		$(find /sys/devices -name driver | sed -e "s/\/driver//")')
	TSORTED_DEV_LIST=$(echo "${tsort_edges}" | tsort | sed -e "s/\"//g")
}

TSORTED_DEV_LIST=
OUTPUT_LIST=

while getopts "s:rlh" f; do
	case "$f" in
	s) export ANDROID_SERIAL="${OPTARG}" ;;
	r) REVERSE_DRIVER_DEP_LIST=1 ;;
	l) WANT_MOD_LOAD_ORDER=1 ;;
	h) echo "${USAGE}" && exit 0 ;;
	esac
done

if [ $REVERSE_DRIVER_DEP_LIST = 1 ] && [ $WANT_MOD_LOAD_ORDER = 1 ]; then
	echo "${USAGE}"
	exit 1
elif [ ! -f "${DEV_NEEDS_HOST_PATH}" ]; then
	echo "Kernel does not contain the required dev-needs.sh script"
	exit 1
fi

if ! prepare_device; then
	exit 1
fi

get_tsorted_dev_list

if [ $WANT_MOD_LOAD_ORDER = 1 ]; then
	devs_to_module_list "${TSORTED_DEV_LIST}"
else
	devs_to_compat_list "${TSORTED_DEV_LIST}"
fi

remove_duplicate_list_entries "${OUTPUT_LIST}"

if [ $REVERSE_DRIVER_DEP_LIST = 1 ]; then
	echo "${OUTPUT_LIST}" | tac
else
	echo "${OUTPUT_LIST}"
fi

cleanup_device
