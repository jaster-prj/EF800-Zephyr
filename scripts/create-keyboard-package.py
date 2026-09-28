#!/usr/bin/env python3
"""Create an application-only EF800 keyboard update package."""
import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0x4B504645
FORMAT = 1
TARGET = 0x0435
BASE = 0x08008000
LIMIT = 0x38000

parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--version", required=True, help="major.minor.patch")
parser.add_argument("--force", action="store_true")
args = parser.parse_args()

version = tuple(int(part) for part in args.version.split("."))
if len(version) != 3 or any(part < 0 or part > 255 for part in version):
    raise SystemExit("version must be major.minor.patch with byte-sized parts")
payload = args.image.read_bytes()
if not payload or len(payload) > LIMIT:
    raise SystemExit(f"image must be 1..{LIMIT} bytes")
flags = 1 if args.force else 0
header = struct.pack("<IHHIIII BBBBI", MAGIC, FORMAT, 32, TARGET, BASE,
                     len(payload), zlib.crc32(payload) & 0xffffffff,
                     *version, 0, flags)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(header + payload)
