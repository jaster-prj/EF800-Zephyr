#!/usr/bin/env bash

set -eu

build_dir="${1:-application/build/ef800-hwdebug}"
stat_file="${build_dir}/zephyr/zephyr.stat"
bin_file="${build_dir}/zephyr/zephyr.bin"

actual_address="$(awk '$3 == "rom_start" { print $5; exit }' "${stat_file}")"
if [ "${actual_address}" != "08000000" ]; then
	echo "hwdebug: expected rom_start 08000000, found ${actual_address:-missing}" >&2
	exit 1
fi

actual_size="$(stat -c '%s' "${bin_file}")"
if [ "${actual_size}" -gt 65536 ]; then
	echo "hwdebug: image size ${actual_size} exceeds 65536 bytes" >&2
	exit 1
fi

echo "hwdebug: rom_start=${actual_address}, size=${actual_size}/65536 bytes"
