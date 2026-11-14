#!/usr/bin/env python3
"""Build, verify, and version EF800 full-device firmware packages."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


PACKAGE_MAGIC = 0x31574645  # "EFW1" in the little-endian package header.
PACKAGE_FORMAT_VERSION = 1
PACKAGE_HEADER_SIZE = 64
PACKAGE_HEADER_CRC_OFFSET = 38
PACKAGE_HEADER_FORMAT = "<I5H6H4I22s"
INITIAL_VERSION = (1, 0, 0)
ROM_START_PATTERN = re.compile(
    r"^\s*\[\s*\d+\]\s+rom_start\s+PROGBITS\s+([0-9A-Fa-f]+)\s",
    re.MULTILINE,
)


@dataclass(frozen=True)
class Target:
    key: str
    target_id: int
    filename: str
    build_dir: str
    hardware_id: int
    load_address: int
    image_limit: int
    coordinator_id: int


TARGETS = (
    Target("ef800-bootloader", 1, "EF800_BOOT.pkg", "ef800-bootloader",
           0x0461, 0x08000000, 0x00010000, 2),
    Target("ef800-backup", 2, "EF800_BACKUP.pkg", "ef800-backup",
           0x0461, 0x08010000, 0x00020000, 2),
    Target("ef800-application", 3, "EF800_APP.pkg", "ef800-application",
           0x0461, 0x08030000, 0x000D0000, 1),
    Target("keyboard-updater", 4, "EF800_KEYBOARD_UPDATER.pkg",
           "keyboard-updater", 0x0435, 0x08038000, 0x00008000, 2),
    Target("keyboard-bootloader", 5, "EF800_KEYBOARD_BOOT.pkg",
           "keyboard-bootloader", 0x0435, 0x08000000, 0x00008000, 2),
    Target("keyboard-application", 6, "EF800_KEYBOARD_APP.pkg",
           "keyboard-application", 0x0435, 0x08008000, 0x00030000, 2),
)

COORDINATOR_KEYS = {
    1: "ef800-bootloader",
    2: "ef800-application",
}


class PackageError(ValueError):
    """Raised when an image, manifest, or package violates the contract."""


def parse_version(value: object, context: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not re.fullmatch(r"\d+\.\d+\.\d+", value):
        raise PackageError(f"{context}: expected semantic version MAJOR.MINOR.PATCH")
    version = tuple(int(part) for part in value.split("."))
    if any(part > 0xFFFF for part in version):
        raise PackageError(f"{context}: each version component must fit in 16 bits")
    return version


def load_versions(path: Path) -> dict[str, dict[str, tuple[int, int, int]]]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError(f"cannot read version manifest {path}: {error}") from error

    if not isinstance(manifest, dict) or manifest.get("format_version") != 1:
        raise PackageError(f"{path}: unsupported or malformed manifest format")
    images = manifest.get("images")
    expected_keys = {target.key for target in TARGETS}
    if not isinstance(images, dict) or set(images) != expected_keys:
        raise PackageError(
            f"{path}: images must define exactly {', '.join(sorted(expected_keys))}"
        )

    parsed: dict[str, dict[str, tuple[int, int, int]]] = {}
    for key, values in images.items():
        if not isinstance(values, dict):
            raise PackageError(f"{path}: image entry {key} must be an object")
        parsed[key] = {
            "version": parse_version(values.get("version"), f"{key}.version"),
            "minimum_coordinator_version": parse_version(
                values.get("minimum_coordinator_version"),
                f"{key}.minimum_coordinator_version",
            ),
        }
    return parsed


def find_rom_start(build_dir: Path) -> int:
    stat_path = build_dir / "zephyr" / "zephyr.stat"
    try:
        stat_text = stat_path.read_text(encoding="utf-8")
    except OSError as error:
        raise PackageError(f"cannot read linked image metadata {stat_path}: {error}") from error
    match = ROM_START_PATTERN.search(stat_text)
    if match is None:
        raise PackageError(f"{stat_path}: rom_start section is missing")
    return int(match.group(1), 16)


def pack_header(
    target: Target,
    version: tuple[int, int, int],
    minimum_coordinator_version: tuple[int, int, int],
    image: bytes,
) -> bytes:
    fields = (
        PACKAGE_MAGIC,
        PACKAGE_FORMAT_VERSION,
        PACKAGE_HEADER_SIZE,
        target.target_id,
        target.coordinator_id,
        target.hardware_id,
        *version,
        *minimum_coordinator_version,
        target.load_address,
        len(image),
        zlib.crc32(image) & 0xFFFFFFFF,
        0,
        bytes(22),
    )
    header = struct.pack(PACKAGE_HEADER_FORMAT, *fields)
    header_crc = zlib.crc32(header) & 0xFFFFFFFF
    fields = (*fields[:-2], header_crc, fields[-1])
    header = struct.pack(PACKAGE_HEADER_FORMAT, *fields)
    if len(header) != PACKAGE_HEADER_SIZE:
        raise PackageError("internal error: package header size does not match contract")
    return header


def verify_package(
    data: bytes, target: Target
) -> tuple[tuple[int, int, int], tuple[int, int, int], bytes]:
    if len(data) < PACKAGE_HEADER_SIZE:
        raise PackageError(f"{target.filename}: package is shorter than its header")
    fields = struct.unpack(PACKAGE_HEADER_FORMAT, data[:PACKAGE_HEADER_SIZE])
    (
        magic, format_version, header_size, target_id, coordinator_id, hardware_id,
        major, minor, patch, min_major, min_minor, min_patch, load_address,
        image_size, image_crc, header_crc, reserved,
    ) = fields

    header = bytearray(data[:PACKAGE_HEADER_SIZE])
    header[PACKAGE_HEADER_CRC_OFFSET:PACKAGE_HEADER_CRC_OFFSET + 4] = bytes(4)
    if magic != PACKAGE_MAGIC or format_version != PACKAGE_FORMAT_VERSION:
        raise PackageError(f"{target.filename}: bad magic or unsupported package format")
    if header_size != PACKAGE_HEADER_SIZE or target_id != target.target_id:
        raise PackageError(f"{target.filename}: wrong header size or target ID")
    if coordinator_id != target.coordinator_id or hardware_id != target.hardware_id:
        raise PackageError(f"{target.filename}: coordinator or hardware ID mismatch")
    if load_address != target.load_address:
        raise PackageError(f"{target.filename}: wrong load address")
    if reserved != bytes(22) or zlib.crc32(header) & 0xFFFFFFFF != header_crc:
        raise PackageError(f"{target.filename}: reserved bytes or header CRC is invalid")
    if image_size == 0 or image_size > target.image_limit:
        raise PackageError(f"{target.filename}: image size exceeds target bounds")
    if len(data) != header_size + image_size:
        raise PackageError(f"{target.filename}: package length does not match its header")

    image = data[header_size:]
    if zlib.crc32(image) & 0xFFFFFFFF != image_crc:
        raise PackageError(f"{target.filename}: image CRC is invalid")
    return (major, minor, patch), (min_major, min_minor, min_patch), image


def verify_previous_packages(
    previous_dir: Path,
    images: dict[str, bytes],
    versions: dict[str, dict[str, tuple[int, int, int]]],
) -> None:
    for target in TARGETS:
        previous_path = previous_dir / target.filename
        try:
            previous_data = previous_path.read_bytes()
        except OSError as error:
            raise PackageError(
                f"previous package {previous_path} is required: {error}"
            ) from error
        previous_version, _, previous_image = verify_package(previous_data, target)
        current_version = versions[target.key]["version"]
        if current_version < previous_version:
            raise PackageError(
                f"{target.key}: version {current_version} is older than "
                f"previous {previous_version}"
            )
        if images[target.key] != previous_image and current_version <= previous_version:
            raise PackageError(
                f"{target.key}: image changed but version was not incremented "
                f"above {previous_version}"
            )


def verify_package_directory(
    package_dir: Path,
    versions_path: Path,
) -> None:
    versions = load_versions(versions_path)
    for target in TARGETS:
        package_path = package_dir / target.filename
        try:
            package_data = package_path.read_bytes()
        except OSError as error:
            raise PackageError(f"cannot read package {package_path}: {error}") from error

        package_version, minimum_coordinator_version, _ = verify_package(
            package_data, target
        )
        coordinator_key = COORDINATOR_KEYS[target.coordinator_id]
        expected_minimum = versions[target.key]["minimum_coordinator_version"]
        if package_version != versions[target.key]["version"]:
            raise PackageError(
                f"{target.filename}: version {package_version} does not match "
                f"manifest {versions[target.key]['version']}"
            )
        if minimum_coordinator_version != expected_minimum:
            raise PackageError(
                f"{target.filename}: minimum coordinator version does not match manifest"
            )
        if versions[coordinator_key]["version"] < minimum_coordinator_version:
            raise PackageError(
                f"{target.filename}: coordinator version is below package minimum"
            )
        print(f"{target.filename}: verified, version {'.'.join(map(str, package_version))}")

    bootstrap_path = package_dir / "EF800_FW.bin"
    if bootstrap_path.exists():
        try:
            bootstrap = bootstrap_path.read_bytes()
            app_package = (package_dir / "EF800_APP.pkg").read_bytes()
        except OSError as error:
            raise PackageError(f"cannot read bootstrap verification input: {error}") from error
        _, _, app_image = verify_package(app_package, next(
            target for target in TARGETS if target.key == "ef800-application"
        ))
        if bootstrap != app_image:
            raise PackageError("EF800_FW.bin does not match the EF800_APP.pkg payload")
        if versions["ef800-application"]["version"] != INITIAL_VERSION:
            raise PackageError("EF800_FW.bin is only valid for the 1.0.0 bootstrap")
        print(f"EF800_FW.bin: verified, {len(bootstrap)} bytes")


def build_packages(
    project_root: Path,
    build_root: Path,
    output_dir: Path,
    version_path: Path,
    previous_dir: Path | None,
    initial_release: bool,
) -> None:
    if initial_release == (previous_dir is not None):
        raise PackageError(
            "select exactly one of --initial-release or --previous-package-dir"
        )

    versions = load_versions(version_path)
    if initial_release:
        for target in TARGETS:
            if versions[target.key]["version"] != INITIAL_VERSION:
                raise PackageError(
                    f"{target.key}: initial release version must be 1.0.0"
                )

    images: dict[str, bytes] = {}
    for target in TARGETS:
        build_dir = build_root / target.build_dir
        image_path = build_dir / "zephyr" / "zephyr.bin"
        try:
            image = image_path.read_bytes()
        except OSError as error:
            raise PackageError(f"cannot read firmware image {image_path}: {error}") from error
        if not image or len(image) > target.image_limit:
            raise PackageError(
                f"{target.key}: image size must be 1..{target.image_limit} bytes"
            )
        linked_address = find_rom_start(build_dir)
        if linked_address != target.load_address:
            raise PackageError(
                f"{target.key}: expected rom_start 0x{target.load_address:08x}, "
                f"found 0x{linked_address:08x}"
            )
        images[target.key] = image

    if previous_dir is not None:
        verify_previous_packages(previous_dir, images, versions)

    packages: dict[str, bytes] = {}
    for target in TARGETS:
        coordinator_key = COORDINATOR_KEYS[target.coordinator_id]
        coordinator_version = versions[coordinator_key]["version"]
        minimum_version = versions[target.key]["minimum_coordinator_version"]
        if coordinator_version < minimum_version:
            raise PackageError(
                f"{target.key}: coordinator {coordinator_version} is below "
                f"minimum {minimum_version}"
            )
        header = pack_header(
            target,
            versions[target.key]["version"],
            minimum_version,
            images[target.key],
        )
        package = header + images[target.key]
        verify_package(package, target)
        packages[target.filename] = package

    if initial_release:
        # This raw image is consumed only by the currently deployed legacy bootloader.
        packages["EF800_FW.bin"] = images["ef800-application"]

    output_dir.mkdir(parents=True, exist_ok=True)
    for filename, content in packages.items():
        (output_dir / filename).write_bytes(content)
        print(f"{filename}: {len(content)} bytes")


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-root",
        type=Path,
        default=project_root / "build",
        help="directory containing the six named Zephyr build directories",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=project_root / "artifacts",
        help="directory for generated packages (default: artifacts)",
    )
    parser.add_argument(
        "--versions",
        type=Path,
        default=project_root / "firmware-versions.json",
        help="per-image semantic and minimum coordinator versions",
    )
    release_mode = parser.add_mutually_exclusive_group()
    release_mode.add_argument(
        "--initial-release",
        action="store_true",
        help="require all image versions to be 1.0.0 and emit EF800_FW.bin bootstrap",
    )
    release_mode.add_argument(
        "--previous-package-dir",
        type=Path,
        help="require version increments when an image differs from these six packages",
    )
    release_mode.add_argument(
        "--verify-dir",
        type=Path,
        help="verify the six generated packages and optional legacy bootstrap in a directory",
    )
    args = parser.parse_args()

    try:
        if args.verify_dir is not None:
            verify_package_directory(args.verify_dir.resolve(), args.versions.resolve())
            return 0
        if not args.initial_release and args.previous_package_dir is None:
            raise PackageError(
                "select --initial-release, --previous-package-dir, or --verify-dir"
            )
        build_packages(
            project_root,
            args.build_root.resolve(),
            args.output_dir.resolve(),
            args.versions.resolve(),
            args.previous_package_dir.resolve()
            if args.previous_package_dir is not None else None,
            args.initial_release,
        )
    except PackageError as error:
        print(f"package-firmware: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
