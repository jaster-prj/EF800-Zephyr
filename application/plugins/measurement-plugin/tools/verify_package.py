#!/usr/bin/env python3
"""Strictly verify EF800 measurement PMP metadata and its ELF descriptor."""
import argparse
import struct
import sys
import zlib
from pathlib import Path

from package_plugin import IDS


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def elf_sections(image: bytes):
    if image[:4] != b"\x7fELF" or image[4:6] != b"\x01\x01":
        raise ValueError("image is not little-endian ELF32")
    if u16(image, 16) != 1 or u16(image, 18) != 40:
        raise ValueError("image must be ARM ET_REL")
    section_offset, entry_size, count = u32(image, 32), u16(image, 46), u16(image, 48)
    strings_index = u16(image, 50)
    headers = [image[section_offset + i * entry_size:section_offset + (i + 1) * entry_size]
               for i in range(count)]
    strings_header = headers[strings_index]
    strings = image[u32(strings_header, 16):u32(strings_header, 16) + u32(strings_header, 20)]
    result = []
    for header in headers:
        start = u32(header, 0)
        end = strings.find(b"\0", start)
        name = strings[start:end].decode("ascii") if start < len(strings) else ""
        offset, size = u32(header, 16), u32(header, 20)
        result.append((name, u32(header, 4), offset, size, u32(header, 24), u32(header, 36)))
    return result


def undefined_symbols(image: bytes, sections) -> list[str]:
    names = []
    for _, kind, offset, size, link, entsize in sections:
        if kind != 2 or entsize == 0:
            continue
        string_offset, string_size = sections[link][2], sections[link][3]
        strings = image[string_offset:string_offset + string_size]
        for pos in range(offset, offset + size, entsize):
            name_offset, section_index = u32(image, pos), u16(image, pos + 14)
            if section_index == 0 and name_offset != 0:
                end = strings.find(b"\0", name_offset)
                names.append(strings[name_offset:end].decode("ascii"))
    return names


def unsupported_relocations(image: bytes, sections) -> list[int]:
    types = []
    for _, kind, offset, size, _, entsize in sections:
        if kind != 9 or entsize == 0:  # SHT_REL
            continue
        for pos in range(offset, offset + size, entsize):
            relocation_type = u32(image, pos + 4) & 0xFF
            if relocation_type != 2:  # R_ARM_ABS32 is the loader's only type.
                types.append(relocation_type)
    return types


def verify(path: Path, expected_id: int | None) -> None:
    package = path.read_bytes()
    if len(package) < 108 or u32(package, 0) != 0x504D504B:
        raise ValueError("invalid PMP magic/header")
    if u16(package, 4) != 1 or u16(package, 6) != 108 or u32(package, 8) != len(package):
        raise ValueError("invalid PMP version, header size, or total size")
    plugin_id = u32(package, 48)
    image_offset, image_size = u32(package, 56), u32(package, 60)
    service_offset, service_count = u32(package, 92), u32(package, 96)
    if expected_id is not None and plugin_id != expected_id:
        raise ValueError(f"plugin ID {plugin_id} != {expected_id}")
    if package[12:28] != IDS["application"] or package[28:44] != IDS["interface"]:
        raise ValueError("package application/interface UUID mismatch")
    if u32(package, 44) != 1 or u16(package, 52) != 1:
        raise ValueError("interface version or target architecture mismatch")
    if service_count != 1 or package[service_offset:service_offset + 16] != IDS["port"]:
        raise ValueError("package must require exactly the Port service")
    image = package[image_offset:image_offset + image_size]
    if len(image) != image_size or zlib.crc32(image) & 0xFFFFFFFF != u32(package, 72):
        raise ValueError("image range or CRC mismatch")
    sections = elf_sections(image)
    undefined = undefined_symbols(image, sections)
    if undefined:
        raise ValueError("undefined ELF symbols: " + ", ".join(undefined))
    relocations = unsupported_relocations(image, sections)
    if relocations:
        raise ValueError("unsupported ARM relocations: " +
                         ", ".join(str(value) for value in sorted(set(relocations))))
    rodata = next((image[o:o + s] for n, _, o, s, _, _ in sections if n == ".rodata"), None)
    if rodata is None or len(rodata) < 96 or u32(rodata, 0) != 0x53444D50:
        raise ValueError("missing wire descriptor at start of .rodata")
    required_offset, required_count = u32(rodata, 52), u32(rodata, 48)
    if rodata[8:24] != IDS["application"] or rodata[24:40] != IDS["interface"]:
        raise ValueError("descriptor UUID does not match package")
    if u32(rodata, 40) != 1 or u32(rodata, 44) != plugin_id:
        raise ValueError("descriptor version/plugin ID does not match package")
    if required_count != 1 or rodata[required_offset:required_offset + 16] != IDS["port"]:
        raise ValueError("descriptor Port service does not match package")
    print(f"verified {path}: plugin={plugin_id}, image={image_size} bytes, crc=0x{u32(package, 72):08x}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--plugin-id", type=int)
    args = parser.parse_args()
    try:
        verify(args.package, args.plugin_id)
    except (ValueError, IndexError, struct.error) as error:
        print(f"verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
