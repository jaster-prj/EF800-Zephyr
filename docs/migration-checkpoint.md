# Migration Checkpoint

## Checkpoint State

This file records the remaining hardware and product gates after the completed
routing, protocol, persistence, and measurement increments. The implementation
is buildable and verified. Completed software work is summarized in
`docs/migration-status.md` and is not repeated as active plan work here.

- The current tree verifies twenty-six native tests, all four firmware images,
  and the image checker.
- Current verified sizes are bootloader `35592`, backup `53644`, main `119316`,
  and keyboard `21820` bytes.
- The backup image uses about 82 percent of its 64 KiB partition and is
  size-sensitive.
- These figures are the last recorded software verification and must be rerun
  before a release baseline.
- A temporary `ef800-hwdebug` image is available at `0x08000000` for controlled
  USB CDC and read-only SPI/QSPI/QPI bring-up. It is not part of the normal
  four-image release set.

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

## Current Flash Boundary

The current image set may be flashed only to one sacrificial or fully
recoverable engineering target with SWD or equivalent recovery access. It is
not a production image set. The temporary `ef800-hwdebug` image uses the full
1 MiB internal flash at `0x08000000` and replaces the complete normal image
layout while it is installed.

- Save internal flash, option bytes, and available QSPI contents before
  flashing.
- Build all four images and run `scripts/verify-images.sh` immediately before
  flashing.
- Flash bootloader at `0x08000000`, BackupApplication at `0x08010000`, and
  MainApplication at `0x08030000`. Flash the keyboard image to the L431
  controller application region.
- Never flash the MainApplication binary at `0x08000000`.
- Keep the USB host disconnected. BackupApplication currently enables MSC over
  the unverified QSPI disk, and a main watchdog failure can enter the backup
  image.
- Leave MainApplication FAT and bootloader update storage disabled during first
  bring-up.

The strategy is controlled flash-first for hardware discovery, while
native-testable feature development continues in parallel. Do not deploy the
current images broadly and do not postpone all target testing until feature
work is complete.

The first connected L496 engineering target was explicitly erased and verified
blank before the controlled hardware run. Its option-byte snapshot is recorded
in `docs/stm32l496-target-snapshot.md`; the temporary `ef800-hwdebug` image was
subsequently installed at `0x08000000` and the cleaned download verified
successfully after the external flash was renewed. The latest external-flash
results are recorded in `docs/hardware-debug-qspi.md`; all SPI, QSPI, and QPI
receive tests still return zeroes. The target is currently held halted through
SWD. The legacy recovery captures remain available under `Backup/`.

## Open Hardware And Product Problems

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
  fixed on `USART1` or is part of the dynamic application console contract, and
  capture both paths during bring-up.
- Legacy migration has only been exercised with synthetic native fixtures. A
  production device dump is required to confirm the deployed Mbed enum and
  padding representation before relying on automatic migration.
- The recorded target uses `IWDG_SW=0` (hardware independent watchdog),
  `IWDG_STOP=1`, `IWDG_STDBY=1`, and `WWDG_SW=1` (software window watchdog).
  Every image used on this target must be checked against that watchdog state
  before flashing.

Additional hardware validation is required for the oscillator, QSPI device and
pin controller, MCP23017, T6963, USB VBUS/switch, MAX17043, DS3231, keyboard
controller, confirmed UARTs, external device protocols, and physical
Workspace/Position storage.

## Resume Order

1. Confirm the UART, alternate Port 3, BlueNRG/SPI1, oscillator, QSPI, USB,
   MCP23017, LCD, and keyboard wiring against the schematic.
2. Save the existing target contents and flash `ef800-hwdebug` at
   `0x08000000` on one recoverable target. Use its USB CDC console and passive
   current-state probe first, then its recovery-wrapped SPI, QSPI, and QPI
   tests to identify and validate the external flash before flashing the normal
   image set.
3. Validate SPI JEDEC/SFDP, QSPI `1-1-4`/`1-4-4`, QPI `4-4-4`, configured
   geometry, stable reads, and the electrical levels of `NCS#`, `IO2/WP#`, and
   `IO3/HOLD#/RESET#`. Keep the USB host disconnected from any normal
   BackupApplication MSC image.
4. Restore the normal image set and validate boot handoff, watchdog fallback,
   console capture, keyboard, LCD, RTC, battery, MCP23017, LED, and confirmed
   UART paths.
5. Add confirmed DeviceTree/pinctrl states for active Port 2/3 modes and the
   fixed BlueNRG device without reserving inactive alternate resources.
6. Validate Elta3 and FTS3XX serial protocols and migrated serial configuration
   on hardware.
7. Resolve USB CDC, MainApplication FAT/MSC ownership, and QSPI/FAT behavior.
   Keep storage options hardware-gated until then.
8. Capture deployed Mbed persistence fixtures and validate compatibility against
   the production toolchain output.
9. Define image signing and update-package policy, then rerun the native suite,
   all four firmware builds, image checker, and update-package checks before
   declaring a release baseline.

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
