# EF800 Migration Status

This is the authoritative migration plan and status document. Historical bench
records remain in `docs/hardware-debug-qspi.md` and
`docs/stm32l496-target-snapshot.md`; their dated observations must not be read
as the current firmware state.

## Current State

The Zephyr port is at a controlled hardware bring-up checkpoint. The normal
boot chain and MainApplication run on the recoverable STM32L496 engineering
target, but the firmware is not a production release.

Latest software baseline:

- Zephyr `v4.4.1`, SDK `1.0.1`, reusable `EF800_Devcontainer`.
- Native Twister suite: **37/37 test cases passed**.
- Bootloader, BackupApplication, MainApplication, and keyboard-controller
  builds pass.
- `scripts/verify-images.sh` passes the fixed image layout.
- MainApplication is linked at `0x08030000` and the keyboard image at
  `0x08000000` on the L431 controller.

Latest controlled hardware observations:

- L496 probe `066FFF504982494867074032`, device ID `0x461`.
- L431 probe `0672FF504955857567234934`, device ID `0x435`.
- Bootloader jumps to MainApplication and services the hardware-selected IWDG.
- MCP23017 at `0x24` initializes and controls keyboard/LCD signals.
- DS3231 at `0x68` reads time after bootloader handoff.
- T6963 reaches `240x64`, 40-column ready state.
- MainApplication arms keyboard RX before releasing reset and receives `0xaa`.
- Semantic keyboard input works through the normal MainApplication parser.
- QSPI initializes as a 4 MiB device and FAT mounts on the current target.
- The fuel-gauge transaction at `0x36` still fails with `-5`; battery status is
  optional and MainApplication continues without it.

The latest MainApplication display implementation has a legacy-style persistent
header, compact clock, battery icon when valid data exists, separator at `y=8`,
body-only text clearing, and a periodic mutex-protected RTC/battery snapshot.
Menu content starts below one blank separator row. Arrow mappings are:

```text
Left  E0 4B    Up    E0 48
Down  E0 50    Right E0 4D
```

The existing EF800 release framing remains `E0 F0 <code>`.

Plugin integration checkpoint:

- Elta3 and FTS3XX are registered through the EF800 static device-plugin
  registry using the stable persisted builder IDs `2` and `3`.
- MainApplication resolves measurement-capable ports, serial defaults, and
  measurement dispatch through the registry while retaining existing UART
  ownership and protocol implementations.
- The external PluginManager FAT-backed NUCLEO test clean-builds and its generic
  framework host, ARM, SDK, and native-sim tests pass.
- External package loading is not enabled in MainApplication. PluginManager is
  pinned in the EF800 west manifest, and the NUCLEO host builds the ELTA3 and
  FTS3XX packages from `application/plugins/measurement-plugin` in CI.
 - NUCLEO host additionally clean-builds a RAM-only Measurement assignment/menu
   prototype and a static bounded Calculation plugin. Native host policy tests
   pass. The NUCLEO STM32G0B1 is identified as `0x467` and the updated image is
   flashed and verified. COM14 is the console and COM7 is the instrument bridge.
   Root-volume FAT mounting, package reader validation, package loading, plugin
   lifecycle, bounded calculation, and ELTA3/FTS3XX dummy-device measurements
   have now passed. The NOR/FAT setup remains bring-up-only and is not production
   qualified. The observed JEDEC ID is `ef 40 16` / 8 MiB and does not match the
   supplied W25Q128JV 16 MiB datasheet. See
   `application/pluginmanager-test/TEST_ENVIRONMENT.md`.

## Completed Migration

### Build and image contract

- Containerized west workspace and reproducible Zephyr board definitions.
- L496 and L431 board definitions with fixed internal flash partitions.
- Bootloader, BackupApplication, MainApplication, and keyboard-controller
  foundations.
- MainApplication fixed at `0x08030000`; keyboard controller at `0x08000000`.
- Image-size/address checker and CI build coverage.
- Watchdog early feed and bootloader-to-Main vector handoff.
- Backup fallback and boot-failure accounting foundations.

### Keyboard

- Zephyr matrix scanner and UART command implementation.
- Shared raw matrix, translated scancode, event, and protocol definitions.
- MainApplication keyboard host parser with make/break, command frames,
  modifiers, queues, and parser timeout recovery.
- Semantic debug keyboard with persistent virtual keys and release-all cleanup.
- Physical keyboard reset/RX startup ordering fixed so the one-shot `0xaa` is
  not lost.
- Correct translated arrow mapping for the current compatibility protocol.
- Native regression coverage for raw mappings, parser behavior, modifiers,
  and all four arrow events.

### Display and runtime status

- T6963 command surface, GPIO bus implementation, reset handling, readback,
  cleanup error propagation, and bounded status polling.
- Shared board I2C pin electrical configuration for production and hwdebug.
- Persistent legacy-style LCD header and one-row separator.
- Menu body redraws preserve the header; menu entries use rows 3 through 7.
- Menu selection wraps at both ends.
- RTC and battery values are sampled by a periodic runtime service and rendered
  by MainApplication without doing I2C work in the LCD path.
- RTC reads/writes are serialized.

### RTC, ports, persistence, and application foundation

- DS3231 MFD/RTC integration, UTC/local conversion, timezone/DST settings, and
  RTC editor.
- MCP23017 control outputs, port descriptors, active connection validation, and
  UART ownership checks.
- Port 1 and Port 2 user-facing titles are both `RS232 Port`; Port 2 retains
  UART1 and optional hardware flow-control connection labels.
- Elta3 and FTS3XX framing/parsers, serial configuration, measurement workflow,
  fixed-point records, and Workspace append behavior.
- Backend-neutral persistence API with safe names and no-storage behavior.
- Workspace create/select/restore/list/append and Position records.
- Versioned/checksummed timezone, port-assignment, serial, and boot-metadata
  records.
- Legacy Mbed readers, preflight validation, malformed-data preservation, and
  duplicate DebugDevice rejection.

### Hardware-debug image

- Temporary `ef800-hwdebug` image at `0x08000000` for recoverable targets.
- MCP23017, RTC, keyboard, T6963, UART, and raw external-flash diagnostics.
- T6963 test includes text-memory readback.
- Direct HAL tests remain diagnostics only and are not equivalent to the
  production Zephyr paths.

## Verified Commands

Run west only inside the reusable container:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west twister -T application/tests -p native_sim --inline-logs
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-main -d application/build/ef800-main
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-backup -d application/build/ef800-backup
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-bootloader -d application/build/ef800-bootloader
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard -d application/build/ef800-keyboard
docker exec -w /workspace EF800_Devcontainer bash application/scripts/verify-images.sh
```

The normal console is UART4 through CP2102 `COM7`, `115200 8N1`. The hwdebug
CDC console is separate and must not be confused with the normal console.

## Open Hardware Gates

### Peripherals and electrical behavior

- Identify why the fuel gauge at `0x36` returns `-5`; confirm part marking,
  power, address, and register protocol.
- Confirm actual oscillator source and clock tolerance against the board.
- Confirm MCP23017 wiring, output polarity, reset behavior, USB-select polarity,
  and VBUS behavior.
- Confirm DS3231 backup, alarm/update behavior, and interrupt wiring.
- Confirm T6963 timing, pixel bit order, reset timing, and control polarity.
- Confirm keyboard physical matrix scanning, reset, UART framing, typematic,
  physical-keyboard resumption, and all four arrow keys.
- Confirm Port 1/2/3 UART routes and Port 2 RTS/CTS ownership.
- Confirm BlueNRG SPI1 wiring, reset, interrupt, and chip select.

### QSPI, FAT, and USB

- Confirm the fitted NOR part against JEDEC/SFDP and schematic.
- Confirm 4 MiB capacity, erase geometry, program size, maximum frequency,
  signal integrity, and stable reads.
- Reconcile the historical all-zero raw SPI/QSPI/QPI bench result with the
  current driver SFDP/FAT-mount observation; do not claim JEDEC/QSPI
  qualification until measured.
- Validate existing FAT contents without formatting.
- Validate `.configuration` and `data` directories.
- Validate mutually exclusive Main FAT and Backup MSC ownership, including
  VBUS and power-loss transitions.
- Keep Main FAT, update storage, and USB sharing release-gated until complete.

### Migration compatibility and release

- Capture a real deployed Mbed persistence dump and validate enum, padding,
  record length, and toolchain representation.
- Run conversion on a copy and preserve malformed/conflicting source records.
- Validate Workspace and Position behavior using migrated physical data.
- Validate Elta3 and FTS3XX protocols and migrated serial configuration on
  hardware.
- Implement and validate Port 4 CDC behavior if it remains part of the product
  contract.
- Define image signing, update authentication, rollback, and recovery policy.
- Add package/update tests and repeat the complete release verification gate.

## Deferred Architecture

These are improvements, not current migration blockers:

- Convert the MAX17043-compatible helper to a standard Zephyr sensor device.
- Convert the T6963 `zephyr,user` properties to a project-local binding/device.
- Split the keyboard scanner into a Zephyr device and a separate compatibility
  transport while preserving the current protocol.
- Add broader native tests for scanner transitions, queue overflow, runtime
  snapshots, T6963 configuration, and hardware error injection.

## Historical Evidence

- `docs/stm32l496-target-snapshot.md` records the 2026-09-02 mass erase and
  option bytes before the later controlled flash. It is not the current target
  state.
- `docs/hardware-debug-qspi.md` records the earlier all-zero raw flash bench
  result. Keep it as dated evidence while the QSPI discrepancy is investigated.
- `Backup/` contains legacy captures used for compatibility comparison and must
  not be flashed into the current layout without a complete contract review.

## Release Criteria

Do not declare production readiness until all open hardware gates are closed,
the production persistence dump is validated, update policy is defined, and all
of the following pass from a clean build:

- Native Twister suite.
- All four firmware builds.
- `scripts/verify-images.sh`.
- Hardware boot/handoff/watchdog/fallback regression.
- Keyboard, RTC, MCP23017, LCD, fuel gauge, UART, QSPI/FAT, USB, and external
  protocol regression.
