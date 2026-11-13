#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import pathlib
import sys


def main() -> int:
	if len(sys.argv) != 3:
		print("usage: embed_package.py <package.pmp> <output.c>", file=sys.stderr)
		return 2

	package_path = pathlib.Path(sys.argv[1])
	output_path = pathlib.Path(sys.argv[2])
	data = package_path.read_bytes()
	lines = [
		"/* Generated from the PluginManager dummy package. */",
		"#include <stddef.h>",
		"#include <stdint.h>",
		"const uint8_t test_isolation_spike_dummy_package[] = {",
	]
	for start in range(0, len(data), 12):
		chunk = data[start : start + 12]
		lines.append("\t" + ", ".join(f"0x{value:02x}U" for value in chunk) + ",")
	lines.extend(
		[
			"};",
		"const size_t test_isolation_spike_dummy_package_size =",
		"\tsizeof(test_isolation_spike_dummy_package);",
		"",
		]
	)
	output_path.parent.mkdir(parents=True, exist_ok=True)
	output_path.write_text("\n".join(lines), encoding="ascii")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
