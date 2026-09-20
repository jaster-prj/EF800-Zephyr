#!/usr/bin/env bash
set -eu

BUILD_DIR="${1:-application/build/ef800_usb_sample}"
EXPECTED_START="${2:-08000000}"
BIN="${BUILD_DIR}/zephyr/zephyr.bin"
MAP="${BUILD_DIR}/zephyr/zephyr.map"

if [ ! -f "${BIN}" ] || [ ! -f "${MAP}" ]; then
	printf 'missing USB sample build artifacts below %s\n' "${BUILD_DIR}" >&2
	exit 1
fi

size=$(wc -c < "${BIN}")
if [ "${size}" -gt $((0x100000)) ]; then
	printf 'USB sample image is too large: %s bytes\n' "${size}" >&2
	exit 1
fi

if ! grep -q "rom_start[[:space:]]*0x${EXPECTED_START}" "${MAP}"; then
	printf 'USB sample is not linked at 0x%s\n' "${EXPECTED_START}" >&2
	exit 1
fi

printf 'ef800_usb_sample: rom_start=%s, size=%s/1048576 bytes\n' \
	"${EXPECTED_START}" "${size}"
