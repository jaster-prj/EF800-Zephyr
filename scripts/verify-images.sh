#!/usr/bin/env bash

set -eu

bootloader_dir="${1:-application/build/ef800-bootloader}"
backup_dir="${2:-application/build/ef800-backup}"
main_dir="${3:-application/build/ef800-main}"
keyboard_dir="${4:-application/build/ef800-keyboard}"
keyboard_bootloader_dir="${5:-application/build/ef800-keyboard-bootloader}"
keyboard_updater_dir="${6:-application/build/ef800-keyboard-updater}"

verify_image() {
	name="$1"
	build_dir="$2"
	expected_address="$3"
	maximum_size="$4"
	stat_file="${build_dir}/zephyr/zephyr.stat"
	bin_file="${build_dir}/zephyr/zephyr.bin"

	actual_address="$(awk '$3 == "rom_start" { print $5; exit }' "${stat_file}")"
	if [ "${actual_address}" != "${expected_address}" ]; then
		echo "${name}: expected rom_start ${expected_address}, found ${actual_address:-missing}" >&2
		exit 1
	fi

	actual_size="$(stat -c '%s' "${bin_file}")"
	if [ "${actual_size}" -gt "${maximum_size}" ]; then
		echo "${name}: image size ${actual_size} exceeds ${maximum_size} bytes" >&2
		exit 1
	fi

	echo "${name}: rom_start=${actual_address}, size=${actual_size}/${maximum_size} bytes"
}

verify_size() {
	name="$1"
	build_dir="$2"
	maximum_size="$3"
	bin_file="${build_dir}/zephyr/zephyr.bin"
	actual_size="$(stat -c '%s' "${bin_file}")"

	if [ "${actual_size}" -gt "${maximum_size}" ]; then
		echo "${name}: image size ${actual_size} exceeds ${maximum_size} bytes" >&2
		exit 1
	fi

	echo "${name}: size=${actual_size}/${maximum_size} bytes"
}

verify_image bootloader "${bootloader_dir}" 08000000 65536
verify_image backup "${backup_dir}" 08010000 131072
verify_image main "${main_dir}" 08030000 851968
verify_size keyboard "${keyboard_dir}" 262144
verify_image keyboard-bootloader "${keyboard_bootloader_dir}" 08000000 32768
verify_image keyboard-updater "${keyboard_updater_dir}" 08038000 32768
