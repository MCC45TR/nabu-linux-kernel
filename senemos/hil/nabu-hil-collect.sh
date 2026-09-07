#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Read-only evidence collector for Nabu HIL gates.

set -eu

stage=${1:-unspecified}
output_dir=${2:-./nabu-hil-evidence}
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
report="$output_dir/${timestamp}-${stage}.txt"

mkdir -p "$output_dir"
umask 077

run()
{
	printf '\n$ %s\n' "$*"
	"$@" 2>&1 || true
}

read_files()
{
	pattern=$1
	for file in $pattern; do
		[ -f "$file" ] || continue
		printf '%s=' "$file"
		dd if="$file" bs=4096 count=1 2>/dev/null || true
		printf '\n'
	done
}

{
	printf 'stage=%s\nutc=%s\n' "$stage" "$timestamp"
	run uname -a
	run sh -c 'cat /proc/cmdline'
	run sh -c 'cat /etc/os-release'
	run systemd-detect-virt
	run bootctl status
	run systemctl --no-pager --full status iio-sensor-proxy.service
	run systemctl --no-pager --full status hexagonrpcd-sdsp.service
	run systemctl --no-pager --full status rmtfs.service

	printf '\n--- remote processors ---\n'
	read_files '/sys/class/remoteproc/remoteproc*/name'
	read_files '/sys/class/remoteproc/remoteproc*/state'
	read_files '/sys/class/remoteproc/remoteproc*/firmware'

	printf '\n--- IIO discovery and samples ---\n'
	read_files '/sys/bus/iio/devices/iio:device*/name'
	read_files '/sys/bus/iio/devices/iio:device*/in_*_raw'
	read_files '/sys/bus/iio/devices/iio:device*/in_*_input'
	read_files '/sys/bus/iio/devices/iio:device*/in_*_scale'
	read_files '/sys/bus/iio/devices/iio:device*/mount_matrix'
	if command -v timeout >/dev/null 2>&1 && command -v monitor-sensor >/dev/null 2>&1; then
		run timeout 15s monitor-sensor --all
	fi
	run busctl --system introspect net.hadess.SensorProxy /net/hadess/SensorProxy
	printf '\n--- SSC and SAR contract ---\n'
	run busctl --system introspect org.senemos.Nabu.Sar /org/senemos/Nabu/Sar
	if command -v ssccli >/dev/null 2>&1; then
		run ssccli --probe-data-type=proximity
		run ssccli --probe-data-type=sar_sensor
		run ssccli --probe-data-type=sar_algo_1
	fi
	if command -v timeout >/dev/null 2>&1 && command -v nabu-ssc-probe >/dev/null 2>&1; then
		run timeout 8s nabu-ssc-probe sar_sensor
		run timeout 8s nabu-ssc-probe sar_algo_1
	fi

	printf '\n--- input and wake ---\n'
	run sh -c 'cat /proc/bus/input/devices'
	read_files '/sys/class/wakeup/*/name'
	read_files '/sys/class/wakeup/*/event_count'
	read_files '/sys/class/wakeup/*/wakeup_count'

	printf '\n--- USB-C and role switch ---\n'
	read_files '/sys/class/typec/port*/power_role'
	read_files '/sys/class/typec/port*/data_role'
	read_files '/sys/class/typec/port*/port_type'
	read_files '/sys/class/typec/port*-partner/accessory_mode'
	read_files '/sys/class/usb_role/*/role'
	find /sys/bus/usb/devices -maxdepth 2 -type f \
		\( -name product -o -name manufacturer \) -print 2>/dev/null |
		sort | while IFS= read -r file; do
			printf '%s=' "$file"
			sed -n '1p' "$file" || true
		done

	printf '\n--- power and thermal ---\n'
	read_files '/sys/class/power_supply/*/type'
	read_files '/sys/class/power_supply/*/status'
	read_files '/sys/class/power_supply/*/health'
	read_files '/sys/class/power_supply/*/voltage_now'
	read_files '/sys/class/power_supply/*/current_now'
	read_files '/sys/class/power_supply/*/temp'
	read_files '/sys/class/power_supply/*/charge_control_limit'
	read_files '/sys/class/thermal/thermal_zone*/type'
	read_files '/sys/class/thermal/thermal_zone*/temp'

	printf '\n--- media, sound and radio ---\n'
	run media-ctl -p
	run v4l2-ctl --list-devices
	run aplay -l
	run rfkill list
	run iw dev

	printf '\n--- kernel evidence ---\n'
	run sh -c 'dmesg | tail -n 1200'
	run journalctl -b -k --no-pager -n 1200
	run sh -c 'find /sys/fs/pstore -maxdepth 1 -type f -print -exec sed -n "1,400p" {} \;'
} >"$report"

printf '%s\n' "$report"
