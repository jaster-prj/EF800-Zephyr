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
- Temporary read-only `ef800-hwdebug` image with a USB CDC command console and
  raw SPI/QSPI/QPI JEDEC, SFDP, geometry, recovery, and memory-read diagnostics
  at `0x08000000`.
- STM32L496 engineering-target snapshot captured with STM32CubeProgrammer,
  including option bytes and recovery-image hashes; the target was explicitly
  mass-erased and verified blank. See `docs/stm32l496-target-snapshot.md`.

## Current Checkpoint

The routing, protocol, persistence, and measurement increments are implemented
and have passed the recorded native and firmware verification. The migration is
now at the controlled hardware bring-up checkpoint, not at a production or
field-deployment checkpoint.

The intended software topology is five logical ports: UART3 RS232, UART1 with
optional flow control, UART4 plus alternate Port 3 modes, USB CDC/MSD, and fixed
BlueNRG/SPI1. The fitted QSPI device and direct wiring are now identified as
ISSI `IS25LP032D-JTLE-TR`, 32 Mbit / 4 MiB, 3.3 V, on QSPI Bank 1. Its JEDEC
ID, SFDP contents, erase geometry, and read stability still require target
confirmation. Exact alternate pin ownership, the oscillator, and several
peripheral connections also require schematic and target confirmation. Legacy
migration is validated against synthetic fixtures and the observed Mbed ABI
layout, but not yet against a production device dump.

The current images are suitable for a controlled bring-up on one recoverable
engineering target. They are not yet suitable for production flashing.

The recorded engineering target was blank immediately after the snapshot. Its
final Under Reset option-byte read reported `RDP=0xAA` (Level 0),
`FLASH_OPTR=0xFBEEF8AA`, and a hardware-selected IWDG. The later controlled
hardware run installed the temporary `ef800-hwdebug` image at `0x08000000`.
The cleaned 59,608-byte image verified successfully after the external flash
was renewed, but all SPI, QSPI, and QPI flash receive tests still returned
zeroes. The target is currently held halted through SWD and must remain in
debug-image mode until a valid JEDEC or SFDP response and the remaining
hardware gates have been validated. See `docs/hardware-debug-qspi.md` for the
bench result.

## Flash decision and safety boundary

Flash the current image set now only to a sacrificial or fully recoverable
target with SWD or an equivalent programming and recovery connection. Hardware
bring-up must start before more hardware-dependent feature work, because clock,
pin ownership, UART routing, reset behavior, and peripheral polarity cannot be
validated by the native tests.

The current default configurations deliberately keep MainApplication QSPI/FAT
storage and bootloader update storage disabled. BackupApplication does enable
USB MSC and exposes the QSPI disk, so its behavior remains hardware-gated until
the identified part and existing volume have been read and validated.
Therefore:

- Save the target's internal flash, option bytes, and available QSPI contents
  before changing it.
- The recorded target's legacy firmware captures are retained under `Backup/`,
  while the target itself is now blank after an explicit full-chip erase.
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
- Confirm the exact L496 package and board identity; SWD identified the target
  as STM32L496xx/STM32L4A6xx, device ID `0x461`, revision `2.0`.
- For future targets, save the original internal flash, option bytes, and QSPI
  contents where possible before flashing. The recorded engineering target has
  already been erased and is not a production preservation reference.
- Validate bootloader and hardware-debug behavior with the observed mixed
  watchdog selection: IWDG hardware mode, WWDG software mode, and both IWDG
  low-power activity bits enabled.
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

- External QSPI NOR SPI/QSPI/QPI JEDEC ID, SFDP, erase geometry, and maximum
  frequency.
- Actual STM32L496 clock source and board oscillator wiring.
- QSPI pinctrl support for the selected Zephyr revision.
- External pull-ups on `NCS#`, `IO2/WP#`, and `IO3/HOLD#/RESET#` for robust
  startup while the STM32 pins float during reset.
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

- Confirm the identified `IS25LP032D-JTLE-TR` by JEDEC ID or SFDP and retain the
  verified 32-Mbit / 4-MiB board description.
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

## Future driver architecture

The following work is intentionally deferred until the current hardware
bring-up and compatibility behavior are stable. It is a planned architectural
improvement, not a prerequisite for the present controlled-target checkpoint.
The existing implementations and protocol behavior must remain the reference
until each step has matching tests and hardware validation.

### MAX17043 sensor driver

Convert the existing `shared/ef800_drivers/ef800_max17043.*` helper into a
devicetree-instantiated Zephyr sensor driver.

- First check the Zephyr `v4.4.1` tree for an upstream MAX17043 binding or
  driver before adding a project-local implementation.
- Keep the existing `maxim,max17043` board node at `fuel-gauge@36` as the
  hardware description and use its I2C specification directly in the driver.
- Implement the standard sensor fetch and channel operations for cell voltage
  and state of charge, while preserving the existing big-endian register
  decoding and conversion rules.
- Keep the application-level battery status conversion in a small compatibility
  layer so UI code does not depend directly on `struct sensor_value` details.
- Preserve optional-device behavior: an absent or nonresponsive gauge must not
  prevent MainApplication startup.
- Add native tests for register byte order, voltage conversion, SOC clamping,
  sensor channel values, and I2C error propagation.

### T6963 display device

Replace the T6963 properties currently stored under `zephyr,user` with a
project-local, devicetree-backed EF800 T6963 device.

- Add a `jaster,ef800-t6963` binding with the eight data GPIOs, control/reset
  GPIOs, display geometry, font width, and controller memory size.
- Instantiate the existing implementation through Zephyr's device model and
  move GPIO readiness, configuration, reset, and controller initialization into
  the device initialization path.
- Keep the current EF800-specific API for text memory, cursor control,
  character-generator RAM, controller modes, and drawing helpers. These
  operations do not map completely to the generic Zephyr display API.
- Consider a later `display_driver_api` adapter only for capabilities, clear,
  and basic pixel/rectangle operations after the hardware behavior is proven.
- Remove the manual LCD configuration object and `zephyr,user` LCD properties
  from MainApplication once the device API is established.
- Keep T6963 bus timing, pixel bit order, control polarity, and MCP23017-backed
  signal behavior as explicit hardware gates. A successful device build does
  not validate those electrical assumptions.
- Add tests around devicetree-derived configuration and preserve the existing
  pure address helper tests. Hardware testing must cover initialization,
  command timing, reset, text output, and pixel behavior.

### EF800 keyboard device and protocol split

Refactor `ef800-keyboard` into a proper device-backed GPIO scanner while
keeping the legacy EF800 UART protocol as a separate compatibility layer.

- Freeze the current behavior before refactoring: matrix orientation, special
  key mapping, debounce, make/break scancodes, typematic timing, activation,
  debug virtual keys, ACK/RESEND behavior, and release-all cleanup.
- Extract protocol parsing and encoding into pure shared modules that do not
  depend on GPIO, UART, logging, or application state.
- Implement the matrix scanner as a `jaster,ef800-keyboard` device using the
  existing row, column, and special-key GPIO properties. Preserve the current
  scan order and logical-to-scancode mapping during the first migration.
- Evaluate Zephyr's common matrix keyboard infrastructure, but do not replace
  the current scanner until generated key transitions match the compatibility
  tests. The five special GPIO keys require explicit treatment outside a pure
  matrix description.
- Report ordinary physical key transitions through Zephyr's input subsystem
  where a standard key code is unambiguous.
- Keep an EF800-specific API for activation, typematic, debug virtual keys,
  keyboard identification, reset, bootloader entry, and protocol status.
- Move UART configuration, RX buffering, and TX behind a transport layer. The
  transport must not interpret protocol bytes or allow console/log output to
  corrupt the binary keyboard link.
- Treat the `ef800-main` keyboard host as a separate UART/protocol-device
  migration. Initially preserve its translated character and application-event
  queues, then add input-event integration only after compatibility is proven.
- Add native tests for scanner transitions, protocol framing, special keys,
  typematic behavior, virtual-key cleanup, parser recovery, and queue overflow.
- Hardware validation must cover reset, physical scanning, UART framing, debug
  input, and resumption of physical scanning after virtual-key mode.

### Completion criteria

Do not consider these future driver migrations complete until the relevant
increment has passed all of the following:

- Existing native compatibility tests remain green and new driver tests pass.
- All four firmware images build in the reusable development container.
- The changed image layout and binary-size checks still pass.
- Devicetree-disabled configurations continue to build where the peripheral is
  optional.
- Hardware behavior has been validated for the affected GPIO, I2C, display,
  keyboard, and UART paths.
- The migration status and component status documents record the resulting API
  and any remaining hardware assumptions.

## Recommended resume order

1. Confirm schematic, BOM, MCU variants, oscillator, UART routes, QSPI part,
   USB, MCP23017, LCD, BlueNRG, and keyboard wiring.
2. Save the existing target contents and flash one recoverable target with the
   current images at the fixed addresses. For an unknown powered flash state,
   use `ef800-hwdebug` and its passive probe plus recovery-wrapped SPI/QSPI/QPI
   tests before restoring the normal image set.
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
