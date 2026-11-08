# EF800 Testing

The authoritative migration state and release gates are maintained in
`docs/migration-status.md`. This document describes test execution and the
boundary between native and hardware validation.

## Native Tests

Native tests run on `native_sim` and do not require EF800 hardware:

- `tests/shared/keyboard`: keyboard parsing, translation, key names, and host
  streaming behavior.
- `tests/shared/rtc`: RTC conversion, validation, and offset parsing.
- `tests/shared/persistence`: persistence, workspace, position, and legacy
  migration behavior.
- `tests/shared/composition`: bounded target-profile encoding, catalog/default
  resolution, unknown-token preservation, staged updates, restart activation,
  and two-slot recovery behavior.
- `tests/shared/ports`: port descriptors, assignments, and UART ownership.
- `tests/shared/measurement`: measurement protocols, validation, formatting,
  and workspace append behavior.
- `tests/shared/metadata`: boot metadata encoding and validation.
- `tests/shared/core`: focused shared-component tests covering boot target
  priority, FAT/MSC ownership transitions, metadata serialization and
  corruption rejection, and invalid arguments.
- `tests/shared/keyboard_update`: keyboard image target/range checks, update
  framing, ordered chunk handling, truncation, and CRC-failure behavior.
- `tests/shared/virtual_usb`: MainApplication CDC byte exchange, real MTP list/
  upload/download over Zephyr's virtual USB host/device pair, and `USBMSD0`
  shared-data FAT ownership.

The current native baseline is **26 Twister configurations and 174 executed
ztest cases** (latest run: 2026-11-07). The Python Virtual EF800 gateway/browser suite is separate from
Twister and currently contains 23 passing unit/asset checks; its optional
`test_mtp_e2e` test is run against a simulator endpoint when
`EF800_SIMULATOR_HOST` is set.

## Virtual EF800

The `application/ef800-main` target runs the production behavioral EF800 orchestration on
Zephyr `native_sim` without an STM32 controller or physical PCB. It provides a
headless virtual status/display/keyboard foundation and a front-panel transport
on TCP port `8800`. The browser gateway and host tools are under
`GSE/Virtual-EF800`.

Native-sim provides a device-next virtual UDC/UHC bus. The software host uses
the production MainApplication CDC and MTP contexts. Browser file operations
are gateway-mediated MTP transactions over this bus; the gateway does not
access simulator files directly. Main MSC is excluded by native-sim's
`USBMSD0` profile.

Build the target inside the reusable container:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-main -d application/build/ef800-main-native-sim
```

For interactive use, build and start the independent Compose deployment:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-main -d application/build/ef800-main-native-sim
docker compose -f docker-compose.virtual-ef800.yml build
docker compose -f docker-compose.virtual-ef800.yml up -d
Start-Process http://127.0.0.1:8080/
```

The browser starts as a viewer. Acquire control before using the semantic
keypad. Only one browser controls the simulator at a time; disconnect and
restart paths issue `release-all`. The headless `scenario_client.py` remains
available for deterministic acceptance and can connect directly when the web
service is not running.

The production MainApplication workspace browser has an end-to-end native-sim
test. It launches an isolated simulator with a temporary flash image, uploads
mixed CR/LF/CRLF workspace rows through the virtual USB MTP peer, selects the
workspace through the UI, and exercises row selection, bounded editing,
cancel, and confirmed deletion through the production keyboard and LCD path:

```powershell
docker exec -w /workspace/application EF800_Devcontainer /opt/venv/bin/python tests/virtual_frontpanel/test_workspace_browser.py
```

Virtual results qualify covered application control flow, display/keypad
behavior, plugin supervision, cleanup, quarantine, and data-integrity
behavior. They do not qualify STM32 registers, ARM image loading, MPU/W^X,
CPU fault classification, boot handoff, electrical timing, QSPI geometry,
physical USB enumeration/interop, or fitted peripheral wiring. Virtual CDC/MTP
enumeration is software behavior evidence only.

Bootloader recovery behavior is also covered by the firmware control-flow
contract: the failure limit applies only while MainApplication is the selected
target, and a boot following BackupApplication resets the counter before
retrying MainApplication.

All native suites under `tests/` are executed together with one Twister
invocation:

```sh
west twister -T application/tests -p native_sim --inline-logs --no-clean
```

The former aggregate `keyboard_protocol` suite has been decomposed into these
domain suites because it covered shared components rather than only keyboard
behavior.

## Firmware Build Gates

The Bootloader, BackupApplication, MainApplication, keyboard bootloader,
keyboard application, and keyboard updater images are build-tested. The revised
L431 map is bootloader 32 KiB, application 192 KiB, and updater 32 KiB; the
three keyboard images link into their assigned partitions and their vector
tables pass bounds checks. These builds do not execute firmware control flow.

## Hardware-Only Coverage

The 2026-11-07 physical MTP update validation and historical failed
system-ROM keyboard-package attempt are recorded in
[`firmware-update-hardware-validation.md`](firmware-update-hardware-validation.md).
The custom updater is implemented in software: MainApplication streams packages
over I²C, the keyboard application at `0x2a` can update only the updater slot,
and the resident updater at `0x48` can update only the keyboard application or
bootloader. Protocol/range helpers are covered by native tests and all three
L431 images build within their partitions. Neither the I²C transaction timing,
Flash erase/write path, nor three-image handoff has been qualified on hardware.
The three images have been programmed and CubeProgrammer-verified over SWD on
the L431 after the explicit probe/device-ID checks. Normal post-reset boot,
the live 1.1.1 version query, and a virtual-key UART make event were observed;
no keyboard firmware update has yet been validated through the custom updater.
Recheck L431 identity before any later SWD operation.

The package CRC and transport XOR checks detect accidental corruption; neither
provides authenticity or protects against a maliciously modified firmware
package. Hardware interruption/retry behavior, including recovery after a
partially erased or programmed target, remains unverified.

The following require a powered target and cannot be proven by `native_sim`:

- STM32 reset, watchdog, vector-table handoff, and backup fallback
- QSPI JEDEC/SFDP, geometry, FAT mount, and power-loss behavior
- Runtime updater entry, Flash installation, and recovery after interruption
- USB MSC host enumeration and host-side file transfer
- FAT-to-MSC ownership transitions with the real QSPI disk
- Workspace and keyboard-firmware transfer through MSC
- Resident keyboard-controller application/bootloader update transport and
  power-loss recovery
- MCP23017, DS3231, fuel gauge, T6963, status LED, UART routing, and physical
  keyboard electrical behavior
- Bootloader-to-Main handoff, IWDG feeding, and controlled Backup fallback
- QSPI raw protocol qualification, existing FAT contents, and power-loss
  behavior
- FAT directory-rename durability under power loss for journaled Workspace
  replacement; native failure-injection tests do not qualify physical QSPI
  behavior
- The narrow time-of-check/time-of-use window between local MTP conflict
  validation and file promotion; ZephyrMTP currently has no shared mutation gate

Hardware validation must keep FAT and MSC mutually exclusive. The required
sequence is `FAT unmount -> MSC enable` and `MSC disable -> FAT mount`.
