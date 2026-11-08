#!/usr/bin/env python3
"""Create a target-specific EF800 keyboard firmware update package."""
import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0x4B504645
FORMAT = 2
TARGET = 0x0435
BASE = 0x08008000
LIMIT = 0x30000

parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--version", required=True, help="major.minor.patch")
parser.add_argument("--target", choices=("application", "bootloader", "updater"),
                    default="application", help="keyboard image partition")
parser.add_argument("--force", action="store_true")
args = parser.parse_args()

version = tuple(int(part) for part in args.version.split("."))
if len(version) != 3 or any(part < 0 or part > 255 for part in version):
    raise SystemExit("version must be major.minor.patch with byte-sized parts")
if args.target == "application":
    image_kind, base, limit = 1, 0x08008000, 0x30000
elif args.target == "bootloader":
    image_kind, base, limit = 2, 0x08000000, 0x8000
else:
    image_kind, base, limit = 3, 0x08038000, 0x8000
payload = args.image.read_bytes()
if not payload or len(payload) > limit:
    raise SystemExit(f"{args.target} image must be 1..{limit} bytes")
flags = 1 if args.force else 0
header = struct.pack("<IHHIIII BBBBI", MAGIC, FORMAT, 32, TARGET, base,
                     len(payload), zlib.crc32(payload) & 0xffffffff,
                     *version, image_kind, flags)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(header + payload)
