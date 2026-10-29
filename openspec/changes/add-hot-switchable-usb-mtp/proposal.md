## Why

USB MSC gives the host raw ownership of the QSPI disk, forcing MainApplication to unmount FAT and preventing safe simultaneous device-side filesystem access. EF800 needs a Windows- and Linux-compatible file-transfer mode that keeps FAT mounted, while preserving MSC as an explicitly restart-gated compatibility mode.

## What Changes

- Add USB MTP as a Port 4 personality backed by an out-of-tree Zephyr module pinned to an audited snapshot of the pending upstream MTP implementation.
- Expose the existing `/EF800:/data`, `/EF800:/plugins`, and `/EF800:/fwu` directories as three separate MTP storages, with at most 12 objects per storage in the initial release.
- Keep `/EF800:/.configuration` and all other volume content outside the MTP-visible roots.
- Allow runtime switching among no native-USB personality, USB DebugConsole, and USB MTP without restarting EF800; the host observes a USB disconnect and re-enumeration.
- Require a controlled restart for every transition entering or leaving USB MSC so FAT and raw-block ownership are never shared.
- Make FAT access reentrant and define file-completion and validation boundaries for concurrent host and EF800 access.
- Preserve the existing `/EF800:/fwu/EF800_FW.bin` and `/EF800:/fwu/EF800_KEYBOARD.pkg` update paths and reject incomplete or invalid packages before applying them.
- Add Windows and Linux interoperability, transition, concurrency, interrupted-transfer, update-safety, and filesystem-integrity verification.
- Defer support for more than 12 MTP objects per exposed storage.

## Capabilities

### New Capabilities
- `usb-personality-switching`: Selection, persistence, runtime transition, rollback, and restart rules for None, USB DebugConsole, USB MTP, and USB MSC.
- `mtp-filesystem-access`: MTP-visible storage roots, concurrency boundaries, update safety, host interoperability, and the initial object limit.

### Modified Capabilities

None.

## Impact

- Adds a pinned west module containing the temporary MTP class and compatibility glue for Zephyr v4.4.2.
- Changes MainApplication USB lifecycle management, Port 4 descriptors and persisted assignment handling, FAT configuration, and MSC ownership transitions.
- Affects the USB console and storage adapters, port-assignment UI, boot metadata compatibility, QSPI filesystem access, firmware/keyboard update validation, build configuration, native tests, hardware tests, and migration documentation.
- Requires physical STM32L496 testing with Windows and Linux hosts; native simulation cannot establish USB electrical or host-stack interoperability.
