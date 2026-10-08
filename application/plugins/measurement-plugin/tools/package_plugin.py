#!/usr/bin/env python3
"""Build a PluginManager package from a constrained ELF32 ARM image."""
import argparse
import struct
import zlib
from pathlib import Path

IDS = {
    "application": bytes.fromhex("65663830302d6170702d763100000001"),
    "interface": bytes.fromhex("65663830302d6d6561732d7631000001"),
    "port": bytes.fromhex("65663830302d706f72742d7631000001"),
}


def build(image: bytes, plugin_id: int, name: str, version: str,
          ram_size: int, stack_size: int, target: int = 1) -> bytes:
    name_data = name.encode("ascii") + b"\0"
    version_data = version.encode("ascii") + b"\0"
    if not 0 < len(name_data) <= 64 or not 0 < len(version_data) <= 32:
        raise ValueError("name/version exceeds PluginManager limits")
    name_offset = 108
    version_offset = name_offset + len(name_data)
    service_offset = (version_offset + len(version_data) + 3) & ~3
    image_offset = (service_offset + 16 + 3) & ~3
    package = bytearray(image_offset + len(image))
    struct.pack_into("<IHHI", package, 0, 0x504D504B, 1, 108, len(package))
    package[12:28] = IDS["application"]
    package[28:44] = IDS["interface"]
    struct.pack_into("<IIHHIIIIIIIIIII", package, 44, 1, plugin_id, target, 0,
                     image_offset, len(image), ram_size, stack_size,
                     zlib.crc32(image) & 0xFFFFFFFF, name_offset, len(name_data),
                     version_offset, len(version_data), service_offset, 1)
    package[name_offset:name_offset + len(name_data)] = name_data
    package[version_offset:version_offset + len(version_data)] = version_data
    package[service_offset:service_offset + 16] = IDS["port"]
    package[image_offset:] = image
    return bytes(package)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plugin-id", type=int, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--ram-size", type=int, default=2048)
    parser.add_argument("--stack-size", type=int, default=512)
    parser.add_argument("--target", type=int, choices=(1, 2), default=1)
    args = parser.parse_args()
    data = build(args.image.read_bytes(), args.plugin_id, args.name, args.version,
                 args.ram_size, args.stack_size, args.target)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
