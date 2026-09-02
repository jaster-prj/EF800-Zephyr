# Migration Checkpoint

## Checkpoint State

This file records the routing/protocol/persistence increments and their
remaining hardware gates. The implementation is buildable and verified; the
current source and documentation changes are uncommitted.

- Repository HEAD: `c3932be` (`Update plans`).
- Routing and metadata implementation: `bffe7a6` (`Add next iteration of
  migration`). Protocol and measurement implementation: `21e68af`.
- The current tree verifies twenty-six native tests, all four firmware images,
  and the image checker.
- Current verified sizes are bootloader `35592`, backup `53644`, main `119316`,
  and keyboard `21820` bytes.

## Authoritative Hardware Topology

The following corrections supersede earlier assumptions in the provisional
implementation:

- Port 1 is an RS232 port using UART3. It has no real hardware flow-control
  capability because the relevant pins are used by other peripherals.
- Port 2 is a UART1 serial port. It supports plain serial and optional real
  hardware flow control.
- Port 3 is a multi-function connector. Its serial mode uses UART4 and it also
  supports alternate parallel, I2C, and SPI modes.
- Port 4 is USB. The selected device may be CDC debug or USB MSD.
- Port 5 is a fixed BlueNRG device connected to SPI1. It is not a user-selectable
  generic port assignment.
- Alternate connection modes may reuse MCU peripherals and pins. A pin or
  peripheral must not be rejected merely because it appears in an inactive
  alternate mode. Only the selected active mode owns its resources.

## Console And Storage Contract

- Keep the existing Zephyr startup/development console on `USART1`; do not move
  startup console ownership to UART4.
- A persisted DebugDevice selection must be applied as early as possible so
  bootloader, BackupApplication, and MainApplication boot information reaches
  the selected debug target.
- If no DebugDevice is configured and Port 3 is not configured for another
  device/mode, Port 3 is the automatic console fallback in all three L496
  images.
- If Port 3 is occupied, retain the fixed Zephyr `USART1` console as the safe
  fallback.
- The normal application owns QSPI/FAT. If USB VBUS is detected and Port 4 is
  assigned to MSD, MainApplication must unmount FAT before transferring QSPI
  ownership to USB MSC.
- BackupApplication must always provide USB MSC. It must not mount FAT and must
  not require a Port 4 MSD assignment or instantiate a second optional MSD
  device.
- Bootloader update storage remains disabled until the QSPI hardware gates are
  satisfied.

## Current Implementation Areas

The committed increment touches these areas:

 - `shared/ef800_ports/`: port descriptors, connection descriptors, UART route
  registry, debug router, console helper, custom log backend, and provisional
  MainApplication USB MSC support.
 - `shared/ef800_update/`: console target, explicit serialized port metadata,
  and common metadata flash helpers.
- `shared/ef800_main/`: provisional QSPI mount/unmount and USB VBUS/MSD checks.
 - `application/ef800-main/src/main.c`: metadata import/sync, connection-aware
   UART claims, assignment UI, and measurement workflow.
- `application/ef800-main/Kconfig`, `prj.conf`, and `app.overlay`: provisional
  MainApplication USB MSC option and VBUS input.
- `application/ef800-backup/`: unconditional MSC-only behavior; its recovery
  image remains size-sensitive.
- `application/ef800-bootloader/`: shared metadata and console startup support;
  preserve fixed partition and update behavior.
- `boards/ef800/ef800.dts`: provisional UART and QSPI pinctrl definitions.
 - `shared/ef800_measurement/`: Elta3/FTS3XX protocol, transport, and record
   handling.
 - `tests/keyboard_protocol/`: connection, UART-route, metadata, protocol, and
   record tests.

## Resolved In Current Increment

- Metadata APIs use `uint64_t` consistently and encode one builder/connection
  byte per logical port.
- Boot metadata has an explicit 40-byte little-endian representation and CRC
  validation; readers and writers no longer depend on native struct layout.
- Port 5 is initialized and persisted as a non-removable BlueNRG/SPI1 port.
- Builder and active connection compatibility is validated for assignment,
  import, metadata, and UART claim paths.
- MainApplication consumes metadata at startup, reconciles assignments, and
  selects the persisted console with safe fallback behavior.
- Assignment UI supports connection selection and bounds the five-port display.
- Legacy Mbed persistence readers decode bounded little-endian records without
  native-ABI struct casts.
- Migration writes versioned/checksummed timezone, port-assignment, and serial
  configuration records, verifies them by decoding, and rejects duplicate
  DebugDevice assignments before conversion.
- Converted Elta3 and FTS3XX serial settings are loaded when configuring their
  assigned UARTs; defaults remain the fallback when no converted record exists.

## Remaining Problems To Fix

- Port 1 and Port 3 use alternate UART pin/peripheral configurations. The board
  DeviceTree currently exposes USART3 on `PD8/PD9` and UART4 on `PC10/PC11`, but
  does not yet model the required inactive/active alternate pin states for the
  full topology.
- SPI1/BlueNRG DeviceTree wiring, chip-select, reset, interrupt, and exact pin
  assignments are not yet represented. Do not invent them from generic STM32
  pin tables; confirm the schematic or hardware first.
- Port 4 CDC debug is not implemented. The provisional console helper only
  recognizes a CDC-ACM node if one exists, while the current MainApplication
  configuration has no CDC node or CDC transport.
- The provisional MainApplication USB MSC code references USB Kconfig symbols
  that are currently defined only for BackupApplication. Enabling it will
  likely require dedicated MainApplication VID/PID configuration and a clean
  ownership lifecycle.
- The custom log backend redirects logger output, but Zephyr shell/console
  routing remains compile-time based. Decide explicitly whether shell input is
  fixed on `USART1` or is part of the dynamic application console contract.
- Legacy migration has only been exercised with synthetic native fixtures. A
  production device dump is required to confirm the deployed Mbed enum and
  padding representation before relying on automatic migration.

## Resume Order

1. Confirm the UART, alternate Port 3, BlueNRG/SPI1, oscillator, and QSPI
   wiring against the schematic before changing board resources.
2. Add confirmed DeviceTree/pinctrl states for active Port 2/3 modes and the
   fixed BlueNRG device without reserving inactive alternate resources.
3. Validate the Elta3 and FTS3XX serial protocols and migrated serial
   configuration on hardware.
4. Resolve the USB CDC implementation and finalize MainApplication FAT/MSC
   ownership transitions; keep all storage options hardware-gated.
5. Capture deployed Mbed persistence fixtures and validate the compatibility
   readers against the actual production toolchain output.
6. Flash the image set to hardware and validate boot handoff, watchdog fallback,
   LCD, keyboard, RTC, battery, external protocols, QSPI/FAT, and USB behavior.
7. Re-run the native suite, all four firmware builds, image checker, and update
   package checks before changing this checkpoint to a release baseline.

The protocol foundation, assigned-port UART setup, device configuration
persistence/migration, and MainApplication measurement workflow are implemented
and verified in native and firmware builds. The remaining work is validation of
the selected UART routes and framing against hardware, an actual deployed Mbed
file dump, and the QSPI/USB and alternate-port gates above.

## Required Verification Commands

Run west only inside `EF800_Devcontainer`:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/tests/keyboard_protocol -d application/build/keyboard-protocol
docker exec -w /workspace EF800_Devcontainer application/build/keyboard-protocol/zephyr/zephyr.exe
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-main -d application/build/ef800-main
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-backup -d application/build/ef800-backup
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-bootloader -d application/build/ef800-bootloader
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard -d application/build/ef800-keyboard
docker exec -w /workspace EF800_Devcontainer bash application/scripts/verify-images.sh
```
