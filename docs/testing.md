# EF800 Testing

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

Bootloader recovery behavior is also covered by the firmware control-flow
contract: the failure limit applies only while MainApplication is the selected
target, and a boot following BackupApplication resets the counter before
retrying MainApplication.

All native suites under `tests/` are executed together with one Twister
invocation:

```sh
west twister -T application/tests -p native_sim --inline-logs
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

Hardware validation must keep FAT and MSC mutually exclusive. The required
sequence is `FAT unmount -> MSC enable` and `MSC disable -> FAT mount`.
