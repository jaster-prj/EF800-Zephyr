#!/usr/bin/env python3
"""Strictly verify EF800 measurement PMP metadata and its ELF descriptor."""
import argparse
import struct
import sys
import zlib
from pathlib import Path

from package_plugin import IDS

WORKER_ABI_VERSION = 1
WORKER_MAILBOX_SIZE = 16384
WORKER_SERVICE_PORT = 1 << 0
MIN_PLUGIN_RAM_SIZE = 2048
MIN_WORKER_STACK_SIZE = 2048
MPU_ALIGNMENT = 1024
LEGACY_ALIGNMENT = 4
METADATA_SIZE_V4 = 104
METADATA_UUID = bytes.fromhex("65663830302d6d6574612d7631000001")
WORKER_GATEWAY_IMPORT = "ef800_plugin_worker_syscall"


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def elf_sections(image: bytes, target: int):
    if image[:4] != b"\x7fELF" or image[4:6] != b"\x01\x01":
        raise ValueError("image is not little-endian ELF32")
    expected_machine = 40 if target == 1 else 3
    if u16(image, 16) != 1 or u16(image, 18) != expected_machine:
        raise ValueError("image target architecture mismatch")
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
        result.append((name, u32(header, 4), offset, size,
			       u32(header, 24), u32(header, 32), u32(header, 36)))
    return result


def undefined_symbols(image: bytes, sections) -> list[str]:
    names = []
    for _, kind, offset, size, link, _, entsize in sections:
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


def unsupported_relocations(image: bytes, sections, target: int) -> list[int]:
    types = []
    for _, kind, offset, size, _, _, entsize in sections:
        if kind != 9 or entsize == 0:  # SHT_REL
            continue
        expected_relocation = 2 if target == 1 else 1
        for pos in range(offset, offset + size, entsize):
            relocation_type = u32(image, pos + 4) & 0xFF
            if relocation_type != expected_relocation and not (
                    target == 2 and relocation_type == 2):
                types.append(relocation_type)
    return types


def validate_memory_bounds(section_map: dict, ram_size: int,
                           max_text: int, max_rodata: int, max_data: int,
                           max_bss: int, max_shared: int) -> None:
    """Reject missing, inconsistent, or over-policy section declarations."""
    if (max_text <= 0 or max_rodata <= 0 or max_text > 32768 or
            max_rodata > 32768 or max_data > 4096 or max_bss > 4096 or
            max_shared > 4096 or max_data + max_bss > ram_size):
        raise ValueError("invalid or out-of-policy plugin memory declaration")
    actual = {name: section_map.get(name, (0, 0))[0]
              for name in (".text", ".rodata", ".data", ".bss")}
    bounds = {".text": max_text, ".rodata": max_rodata,
              ".data": max_data, ".bss": max_bss}
    for name, size in actual.items():
        if size > bounds[name]:
            raise ValueError(f"{name} exceeds its declared plugin memory bound")
    if actual[".data"] + actual[".bss"] > ram_size:
        raise ValueError("ELF writable sections exceed declared plugin RAM")


def verify(path: Path, expected_id: int | None,
           expected_interface: str = "measurement") -> None:
    package = path.read_bytes()
    if len(package) < 108 or u32(package, 0) != 0x504D504B:
        raise ValueError("invalid PMP magic/header")
    if u16(package, 4) != 1 or u16(package, 6) != 108 or u32(package, 8) != len(package):
        raise ValueError("invalid PMP version, header size, or total size")
    plugin_id = u32(package, 48)
    image_offset, image_size = u32(package, 56), u32(package, 60)
    service_offset, service_count = u32(package, 92), u32(package, 96)
    package_services = package[service_offset:service_offset + service_count * 16]
    if expected_id is not None and plugin_id != expected_id:
        raise ValueError(f"plugin ID {plugin_id} != {expected_id}")
    if expected_interface not in ("measurement", "calculation"):
        raise ValueError("unsupported expected interface")
    if package[12:28] != IDS["application"] or \
            package[28:44] != IDS[expected_interface]:
        raise ValueError("package application/interface UUID mismatch")
    target = u16(package, 52)
    if u32(package, 44) != 1 or target not in (1, 2):
        raise ValueError("interface version or target architecture mismatch")
    image = package[image_offset:image_offset + image_size]
    if len(image) != image_size or zlib.crc32(image) & 0xFFFFFFFF != u32(package, 72):
        raise ValueError("image range or CRC mismatch")
    sections = elf_sections(image, target)
    undefined = undefined_symbols(image, sections)
    relocations = unsupported_relocations(image, sections, target)
    if relocations:
        raise ValueError("unsupported ARM relocations: " +
                         ", ".join(str(value) for value in sorted(set(relocations))))
    rodata = next((image[o:o + s] for n, _, o, s, _, _, _ in sections
                   if n in (".rodata", ".rodata.pm_descriptor")), None)
    if rodata is None or len(rodata) < 96 or u32(rodata, 0) != 0x53444D50:
        raise ValueError("missing wire descriptor at start of .rodata")
    required_offset, required_count = u32(rodata, 52), u32(rodata, 48)
    if rodata[8:24] != IDS["application"] or \
            rodata[24:40] != IDS[expected_interface]:
        raise ValueError("descriptor UUID does not match package")
    if u32(rodata, 40) != 1 or u32(rodata, 44) != plugin_id:
        raise ValueError("descriptor version/plugin ID does not match package")
    if required_count != service_count:
        raise ValueError("package and descriptor service counts differ")

    metadata_offset, metadata_size = u32(rodata, 80), u32(rodata, 84)
    if metadata_size != METADATA_SIZE_V4 or metadata_offset + metadata_size > len(rodata):
        raise ValueError("descriptor has no complete isolation metadata")
    metadata = rodata[metadata_offset:metadata_offset + metadata_size]
    if metadata[:16] != METADATA_UUID or u16(metadata, 16) != 4 or \
            u16(metadata, 18) != METADATA_SIZE_V4:
        raise ValueError("unsupported isolation metadata version")
    model, isolation_abi, metadata_target, services, mailbox_size, ram_size, stack_size, \
        text_align, rodata_align, data_align = struct.unpack_from("<10I", metadata, 44)
    package_ram_size, package_stack_size = u32(package, 64), u32(package, 68)
    max_text, max_rodata, max_data, max_bss, max_shared = struct.unpack_from(
        "<5I", metadata, 84)
    if metadata_target != target:
        raise ValueError("metadata target architecture mismatch")
    if model == 0:  # Existing development packages are explicitly not isolated.
        if isolation_abi != 0 or services != 0 or mailbox_size != 0 or stack_size != 0 or \
                (text_align, rodata_align, data_align) != (LEGACY_ALIGNMENT,) * 3:
            raise ValueError("legacy package makes an invalid isolation claim")
        expected_service_uuid = IDS["port"]
        if expected_interface != "measurement" or service_count != 1:
            raise ValueError("legacy packages must use the measurement Port service")
        if undefined:
            raise ValueError("legacy package contains undefined imports: " +
                             ", ".join(undefined))
    elif model == 1:
        known_services = WORKER_SERVICE_PORT | (1 << 1) | (1 << 2) | (1 << 3)
        if isolation_abi != WORKER_ABI_VERSION or \
                (services & ~known_services) != 0 or \
                mailbox_size != WORKER_MAILBOX_SIZE or stack_size < MIN_WORKER_STACK_SIZE or \
                (text_align, rodata_align, data_align) != (MPU_ALIGNMENT,) * 3:
            raise ValueError("isolated package ABI, service, mailbox, or MPU layout is unsupported")
        if expected_interface == "measurement" and \
                (services & WORKER_SERVICE_PORT) == 0:
            raise ValueError("measurement plugin must declare the Port service")
        expected_count = 0 if services == 0 else 1
        if service_count != expected_count:
            raise ValueError("package service count does not match worker services")
        expected_service_uuid = IDS["worker"] if service_count else b""
        if service_count and undefined != [WORKER_GATEWAY_IMPORT]:
            raise ValueError("isolated package must import only the worker gateway: " +
                             ", ".join(undefined))
        if not service_count and undefined:
            raise ValueError("service-free worker package has undefined imports: " +
                             ", ".join(undefined))
    else:
        raise ValueError("unknown package execution model")
    section_map = {name: (size, alignment)
                   for name, _, _, size, _, alignment, _ in sections}
    required_alignment = MPU_ALIGNMENT if model == 1 else LEGACY_ALIGNMENT
    for section_name in (".text", ".rodata", ".data", ".bss"):
        if section_name in section_map and section_map[section_name][1] < required_alignment:
            raise ValueError(f"{section_name} alignment is below the package declaration")
    validate_memory_bounds(section_map, ram_size, max_text, max_rodata,
                           max_data, max_bss, max_shared)
    if package_services != expected_service_uuid or (service_count != 0 and \
            rodata[required_offset:required_offset + 16] != expected_service_uuid):
        raise ValueError("package and descriptor service declaration mismatch")
    if ram_size < MIN_PLUGIN_RAM_SIZE:
        raise ValueError("package RAM requirement is below the supported minimum")
    if package_ram_size < ram_size or package_stack_size < stack_size:
        raise ValueError("package memory allocation is below declared requirements")
    model_name = "isolated-worker" if model == 1 else "legacy-direct"
    print(f"verified {path}: plugin={plugin_id}, model={model_name}, "
          f"image={image_size} bytes, crc=0x{u32(package, 72):08x}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--plugin-id", type=int)
    parser.add_argument("--interface", choices=("measurement", "calculation"),
                        default="measurement")
    args = parser.parse_args()
    try:
        verify(args.package, args.plugin_id, args.interface)
    except (ValueError, IndexError, struct.error) as error:
        print(f"verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
