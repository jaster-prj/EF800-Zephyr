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
- `tests/shared/ports`: port descriptors, assignments, and UART ownership.
- `tests/shared/measurement`: measurement protocols, validation, formatting,
  and workspace append behavior.
- `tests/shared/metadata`: boot metadata encoding and validation.
- `tests/shared/core`: focused shared-component tests covering boot target
  priority, FAT/MSC ownership transitions, metadata serialization and
  corruption rejection, and invalid arguments.

The current native baseline is 31 test cases. The older 24- and 26-test
figures belong to superseded bring-up notes.

## Virtual EF800

The `application/ef800-main` target runs the production behavioral EF800 orchestration on
Zephyr `native_sim` without an STM32 controller or physical PCB. It provides a
headless virtual status/display/keyboard foundation and a front-panel transport
on TCP port `8800`. The browser gateway and host tools are under
`GSE/Virtual-EF800`.

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

Virtual results qualify covered application control flow, display/keypad
behavior, plugin supervision, cleanup, quarantine, and data-integrity
behavior. They do not qualify STM32 registers, ARM image loading, MPU/W^X,
CPU fault classification, boot handoff, electrical timing, QSPI geometry,
USB enumeration, or fitted peripheral wiring.

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

The Bootloader, BackupApplication, MainApplication, and keyboard-controller
images are build-tested. Image verification checks their linked addresses and
partition limits. A successful build does not execute the firmware control flow.

## Hardware-Only Coverage

The following require a powered target and cannot be proven by `native_sim`:

- STM32 reset, watchdog, vector-table handoff, and backup fallback
- QSPI JEDEC/SFDP, geometry, FAT mount, and power-loss behavior
- Bootloader update installation and image programming
- USB MSC host enumeration and host-side file transfer
- FAT-to-MSC ownership transitions with the real QSPI disk
- Workspace and keyboard-firmware transfer through MSC
- Keyboard-controller firmware update transport
- MCP23017, DS3231, fuel gauge, T6963, status LED, UART routing, and physical
  keyboard electrical behavior
- Bootloader-to-Main handoff, IWDG feeding, and controlled Backup fallback
- QSPI raw protocol qualification, existing FAT contents, and power-loss
  behavior

Hardware validation must keep FAT and MSC mutually exclusive. The required
sequence is `FAT unmount -> MSC enable` and `MSC disable -> FAT mount`.
