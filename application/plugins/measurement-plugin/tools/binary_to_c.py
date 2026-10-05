#!/usr/bin/env python3
"""Embed an arbitrary binary as a const byte array and length."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--symbol")
args = parser.parse_args()
symbol = args.symbol or re.sub(r"[^A-Za-z0-9_]", "_", args.input.stem)
if not re.match(r"^[A-Za-z_]\w*$", symbol):
    raise SystemExit("symbol must be a valid C identifier")
data = args.input.read_bytes()
lines = [f"const unsigned char {symbol}[] = {{"]
for offset in range(0, len(data), 12):
    lines.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 12]) + ",")
lines += ["};", f"const unsigned int {symbol}_size = {len(data)}u;", ""]
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(lines), encoding="ascii", newline="\n")
