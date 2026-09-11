#!/usr/bin/env python3
"""Generate the T6963C-0101 glyph table from the raster text transcription."""
import pathlib
import sys


def main() -> int:
    source = pathlib.Path(sys.argv[1])
    target = pathlib.Path(sys.argv[2])
    rows = [line.split() for line in source.read_text().splitlines() if line.strip()]
    if len(rows) != 64 or any(len(row) != 16 for row in rows):
        raise SystemExit("CG_RAM0101.txt must contain 64 rows of 16 glyphs")
    for row in rows:
        if any(len(glyph) != 8 or set(glyph) - {".", "#"} for glyph in row):
            raise SystemExit("each glyph must be eight characters containing only . and #")

    # The page-26 map is indexed directly by the hexadecimal row/column:
    # 0x21=A, 0x22=B, 0x28=H, 0x29=I, and 0x34=T.
    # These entries must contain pixels and prevent accidental row shifts.
    for code in (0x21, 0x22, 0x28, 0x29, 0x34):
        block, column = divmod(code, 16)
        glyph = [rows[block * 8 + raster][column] for raster in range(8)]
        if not any("#" in raster for raster in glyph):
            raise SystemExit(f"expected populated glyph at code 0x{code:02X}")

    lines = [
        "/* Generated from docs/CG_RAM0101.txt. Do not edit manually. */",
        "static const uint8_t cgrom_0101[128][8] = {",
    ]
    for code in range(128):
        block = code // 16
        column = code % 16
        glyph = [rows[block * 8 + raster][column] for raster in range(8)]
        values = []
        for raster in glyph:
            value = sum((1 << (7 - bit)) for bit, pixel in enumerate(raster) if pixel == "#")
            values.append(f"0x{value:02x}")
        lines.append(f"\t{{{','.join(values)}}}, /* 0x{code:02X} */")
    lines.append("};")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
