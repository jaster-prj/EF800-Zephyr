# Migration status

## Completed migration scope

The following software migration tasks are complete for the currently modeled
EF800 topology. They are recorded here for traceability and are no longer
active items in the migration plan. Their physical behavior still requires the
hardware validation described below.

- Containerized west workspace skeleton.
- EF800 L496 and EF800 keyboard L431 board definitions.
- Legacy internal flash partitions.
- Shared keyboard protocol constants and ASCII/event map.
- Zephyr keyboard-controller scanner and UART command implementation.
- Semantic debug-keyboard shell input with controller-side name-to-matrix and
  matrix-to-scancode resolution, stateful virtual keys, and release-all cleanup.
- Zephyr keyboard-host parser.
- Bootloader and backup application foundations.
- Native keyboard protocol tests.
- MainApplication linked to its fixed `0x08030000` partition.
- Compile-time and CI checks for the fixed image layout and image sizes.
- Backend-neutral persistence API with safe names, read/write/append/stat/list/
  remove operations, and a no-storage Zephyr adapter.
- Workspace create, select, restore-current, list, and append behavior with a
  deterministic native fake backend.
- MainApplication Workspace menu integration with restore, create, select, and
  list actions while storage remains disabled by default.
- Position record formatting and Workspace append behavior using fixed-point
  millimetres.
- MainApplication Position entry dialog gated on a selected Workspace.
- Five logical port descriptors with capability-filtered device assignment.
- Portable, versioned, checksummed port-assignment persistence using the
  backend-neutral storage API.
- MainApplication Settings port-assignment UI with connection-mode selection,
  assignment removal, and device routing startup.
- Fixed 40-byte little-endian boot metadata encoding with CRC validation,
  persisted connection modes, and fixed Port 5 BlueNRG assignment.
- Five-port assignment validation for active connection modes, including UART
  ownership checks and MainApplication connection-mode selection UI.
- MainApplication startup reconciliation for metadata-backed assignments and
  console fallback; bootloader uses the shared metadata read/write contract.
- Elta3 and FTS3XX protocol modules with exact command framing, response
  parsing, fixed-point measurements, serial defaults, record formatting, and
  Workspace append support.
- MainApplication assigned Elta3/FTS3XX UART setup using the persisted
  connection mode and measurement workflow.
- MainApplication Measurement workflow for assigned Elta3/FTS3XX devices:
  trigger, code/staff-height entry, save, discard, and fixed-measurement
  repetition through Workspace.
- Explicit Mbed persistence compatibility readers for legacy time, port
  assignment, DebugDevice, Elta3, and FTS3XX records.
- Versioned/checksummed Zephyr serial configuration records with startup
  loading for converted Elta3 and FTS3XX settings.
- Versioned/checksummed port assignment records use a separate
  `port_assignment_N` namespace so a legacy `port_N` file is not overwritten
  before conversion verification.
- Preflight validation and read-back verification for migration, including
  duplicate DebugDevice rejection and preservation of malformed source data.

## Current Checkpoint

The routing, protocol, persistence, and measurement increments are implemented
and have passed the recorded native and firmware verification. The migration is
now at the controlled hardware bring-up checkpoint, not at a production or
field-deployment checkpoint.

The intended software topology is five logical ports: UART3 RS232, UART1 with
optional flow control, UART4 plus alternate Port 3 modes, USB CDC/MSD, and fixed
BlueNRG/SPI1. Exact alternate pin ownership, the oscillator, QSPI wiring, and
several peripheral connections still require schematic and target
confirmation. Legacy migration is validated against synthetic fixtures and the
observed Mbed ABI layout, but not yet against a production device dump.

The current images are suitable for a controlled bring-up on one recoverable
engineering target. They are not yet suitable for production flashing.

## Flash decision and safety boundary

Flash the current image set now only to a sacrificial or fully recoverable
target with SWD or an equivalent programming and recovery connection. Hardware
bring-up must start before more hardware-dependent feature work, because clock,
pin ownership, UART routing, reset behavior, and peripheral polarity cannot be
validated by the native tests.

The current default configurations deliberately keep MainApplication QSPI/FAT
storage and bootloader update storage disabled. BackupApplication does enable
USB MSC and exposes the QSPI disk, but the QSPI part and geometry are still
placeholders. Therefore:

- Save the target's internal flash, option bytes, and available QSPI contents
  before changing it.
- Build and verify all four images before flashing.
- Flash only at the fixed addresses documented in `docs/flash-layout.md`.
- Keep the USB host disconnected until the QSPI part, capacity, erase geometry,
  programming behavior, and USB ownership have been validated.
- Do not enable MainApplication FAT, bootloader update storage, or USB sharing
  as part of the first bring-up.
- Do not treat a successful build or boot as evidence that QSPI, FAT, USB,
  external protocols, or alternate port modes work.

The recommended strategy is controlled flash-first for hardware bring-up, with
native-testable feature development continuing in parallel. Do not wait until
every feature is implemented before testing the target, and do not deploy the
current images broadly.

## Software verification

Verified with the reusable `EF800_Devcontainer`, Zephyr `v4.4.1`, and Zephyr
SDK `1.0.1`:

- Second-level bootloader builds at `0x08000000` (`35592` bytes).
- Backup application builds at `0x08010000` (`53644` bytes).
- MainApplication builds at `0x08030000` (`119316` bytes).
- Keyboard-controller application builds (`21820` bytes).
- Native keyboard, protocol, RTC, persistence, Position, port-assignment, and
  boot-metadata, measurement protocol, record, and Mbed migration suite passes
  all twenty-six tests.

The backup application currently uses about 82 percent of its 64 KiB
partition. Treat additions to that recovery image as size-sensitive.

## Open migration work

The remaining work is validation and hardware completion, rather than another
large software porting phase.

### Controlled hardware bring-up

- Confirm the exact L496 and L431 parts, board oscillator, power rails, reset,
  SWD access, and schematic pin ownership.
- Save the original internal flash, option bytes, and QSPI contents where
  possible before flashing.
- Validate the bootloader vector checks, main handoff, watchdog accounting, and
  controlled backup fallback.
- Validate keyboard-controller reset, matrix scanning, UART framing, virtual
  key cleanup, and physical-keyboard resumption.
- Validate the status LED, MCP23017 wiring, DS3231, MAX17043, LCD electrical
  timing/polarity, and the confirmed UART routes.
- Capture complete logs. The fixed Zephyr shell remains on USART1, while the
  custom console/log routing can select a configured external target; verify
  both the documented USART1 console and any selected fallback route.

### Board and peripheral completion

- External QSPI NOR part, capacity, erase geometry, and maximum frequency.
- Actual STM32L496 clock source and board oscillator wiring.
- QSPI pinctrl support for the selected Zephyr revision.
- Port 1 and Port 3 alternate UART pin/peripheral states without reserving
  inactive alternate resources.
- Port 2 RTS/CTS pin ownership and runtime UART ownership relative to USART1
  shell access.
- BlueNRG SPI1 chip select, reset, interrupt, and exact pin assignments.
- MCP23017 wiring and interrupt/reset behavior.
- T6963 LCD electrical timing.
- USB switch polarity and USB VBUS behavior.
- Final MainApplication port and external device protocol hardware validation.

### Storage, migration, and release gates

- Identify the fitted QSPI NOR by schematic, JEDEC ID, or SFDP and update the
  board description with verified values.
- Validate FAT mounting and existing contents without formatting, including
  `.configuration` and `data` directory behavior.
- Validate Workspace and Position persistence on the physical QSPI volume.
- Validate mutually exclusive MainApplication FAT and BackupApplication USB
  MSC ownership, including VBUS and power-loss cases.
- Capture persistence files from a deployed Mbed unit and validate enum,
  padding, record-length, and toolchain-specific representation against the
  real production image.
- Define production image signing, update-package authentication, rollback, and
  recovery policy before enabling update storage.
- Re-run the native suite, all four firmware builds, image verification, and
  update-package checks after hardware gates are closed.

## Recommended resume order

1. Confirm schematic, BOM, MCU variants, oscillator, UART routes, QSPI part,
   USB, MCP23017, LCD, BlueNRG, and keyboard wiring.
2. Save the existing target contents and flash one recoverable target with the
   current images at the fixed addresses.
3. Validate boot, watchdog, keyboard, console, RTC, battery, MCP23017, LCD,
   LED, and confirmed UART paths. Keep the USB host disconnected.
4. Add only confirmed DeviceTree and pinctrl resources for the active hardware
   modes.
5. Validate Elta3 and FTS3XX framing and serial configuration on hardware.
6. Validate QSPI/FAT, Workspace/Position persistence, and USB ownership before
   enabling storage or update features.
7. Validate a production Mbed persistence dump and finalize signing/update
   policy.
8. Repeat the complete software and image verification and then declare a
   release baseline.
