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

- Zephyr `v4.4.2`, SDK `1.0.1`, reusable `EF800_Devcontainer`.
- Native Twister suite: **24/24 configurations and 147/147 test cases passed**.
- Bootloader, BackupApplication, MainApplication, keyboard-controller, and
  keyboard-bootloader builds pass; `scripts/verify-images.sh` passes the fixed
  image layout.
- The Virtual EF800 gateway/browser suite reports 45 tests: 43 passed and 2
  skipped in the current container environment;
  a previous optional native-sim Main MTP gateway round-trip passed (5,001-byte
  upload, listing, and download through the virtual USB host and production
  MainApplication MTP class).
- Plugin integration tasks are at **30/33**: contract, discovery, Measurement,
  Calculation, bounded UI, keyboard session, and acceptance artifacts are in
  place. Production plugin security remains deferred.
- Plugin security policy is documented in `docs/plugin-contract/security-policy.md`.
  Unsigned arbitrary executable plugins remain disabled for release.
- MainApplication is linked at `0x08030000` and the keyboard image at
  `0x08000000` on the L431 controller.
- Virtual EF800 behavioral target is available on `native_sim` with a live
  240 x 64 display model, semantic keypad protocol, deterministic peripheral
  controls, and a host front-panel transport. It is deployable with separate
  simulator and localhost browser-gateway containers using Docker Compose.
  This target is behavioral
  evidence only and does not close any STM32, MPU, boot, electrical, QSPI, or
  USB hardware gate.
- The production-composition native-sim target now boots through emulated
  MCP23017, DS3231, MAX17048, GPIO, UART, T6963, keyboard, and FAT-storage
  paths. Its native-sim profile controls remain behavioral evidence only and
  do not qualify physical peripheral wiring, electrical timing, or ARM code.
- Physical and native-sim MainApplication builds now share the
  `application/ef800-main` source manifest; target-specific overlays and
  adapters remain separate.
- LCD contrast mode is selected through the restart-activated COMPOSITION
  `LCDCONTR` group (`LCDCONTR0` default); `LCDCONTR1` uses 40 kHz TIM4_CH3 PWM
  on PB8, with duty persisted separately in private Configuration storage.
  Native-sim publishes contrast state separately from a pixel-only one-bit LCD
  frame; it does not transmit a duplicate semantic text layer.
  This is software implementation evidence only; PB8, PWM voltage, filter
  ripple, and modified-board behavior have not yet been hardware-qualified.
- The Virtual EF800 diagnostics panel now includes bounded, catalog-backed
  COMPOSITION selection controls. Changes are committed through the existing
  profile service and explicitly require simulator restart before activation;
  browser input is never executed as arbitrary shell text.

Partitioned QSPI and Main MTP browser slice:

- The software map is boot-state NVS `0x000000..0x007FFF`, Configuration FAT
  `0x008000..0x0FFFFF`, and Shared Data FAT `0x100000..0x3FFFFF`. Legacy
  monolithic media is detected and left untouched; no automatic repartition or
  format was introduced.
- Internal images use Bootloader `0x08000000/64 KiB`, BackupApplication
  `0x08010000/128 KiB`, and MainApplication `0x08030000/832 KiB`.
- Main MTP exposes Data, Plugins, and FWU. Backup recovery MTP exposes
  Configuration, Data, Plugins, and FWU. Neither exports boot NVS or raw flash.
- Native-sim's `USBMSD0` excludes Main MSC. The browser file view mediates
  approved MTP operations through bounded gateway messages and Zephyr's virtual
  USB host/device pair; it never accesses the simulator filesystem directly.
- The native-sim Port 4 MTP assignment skips the unavailable native UART
  reconciliation path so USB MTP can be activated through the simulated Main
  menu. MTP USB transactions execute on a Zephyr worker, not the POSIX socket
  thread.
- `Show Measurements` supports row selection and raw-text modification or
  confirmed deletion for measurement, position, plugin, empty, and long rows.
  CR, LF, and CRLF endings are recognized and preserved; the inline editor is
  bounded to 255 text bytes.
- Local row updates stream into a sibling temporary file and use alternating,
  checksummed transaction-journal slots plus a backup for startup recovery.
  Native persistence tests inject interruptions through each promotion phase.
- Local edits compare a bounded size/FNV-1a source fingerprint before
  promotion, including while MTP is active. The pinned MTP implementation has
  no shared mutation gate, so writes after the final check can still race with
  promotion. Zephyr FAT exposes file sync but no directory sync; physical
  power-loss durability of rename metadata remains unqualified and release-gated.
- No physical target was flashed for this increment. QSPI geometry, existing
  media migration, power-loss behavior, full physical USB ownership/interruption
  behavior, and production QSPI qualification remain release gates.

BackupApplication recovery-MTP resource check (latest pristine EF800 build):

- `application/build/ef800-backup`: 103,184 B flash (78.72% of the
  128 KiB BackupApplication partition), leaving 27,888 B; static RAM is
  37,528 B (14.32% of the 256 KiB RAM region).
- Zephyr's private `usbd` thread reserves a 4,096 B stack. The linker map shows
  a 4,160 B `usbd_stack` linker object (the configured stack plus 64 B of
  Zephyr/architecture stack overhead or padding), and a separate 128 B
  thread-control block.
- Runtime stack high-water is not measured by this link-time accounting.
  `CONFIG_INIT_STACKS` is disabled in this image, and this software-only change
  does not flash/run a BackupApplication under a sustained physical USB MTP
  workload. OpenSpec task 4.4 remains open until that runtime measurement is
  available; do not treat the configured 4 KiB reservation as observed peak
  use. The current pristine build confirms only link-time flash/RAM footprint;
  task 4.4 remains open for measured runtime high-water evidence.

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
- EF800 USB MTP was physically qualified with Windows and Linux after correcting
  the fitted USB/SPI data-line wiring. CDC and MTP enumerate on both hosts;
  `data`, `plugins`, and `fwu` are visible, files survive reboot, and a later
  MTP session can remove the files. This is initial interoperability evidence,
  not completion of the full transfer/interruption matrix.

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

- The measurement-plugin workflow now keeps code, point number, staff height,
  Save, Discard, and Fixmessung inside the Elta3 and FTS3XX packages. The
  workspace service retains the last measurement point/code context and updates
  it after successful opaque-record appends; MainApplication only transfers
  context and performs the generic append/acknowledgement boundary.
- Plugin host tests cover serialized Save records and Fixmessung point
  advancement for both packages. They are included in the current 24/24,
  147/147 native-sim suite.

- Elta3 and FTS3XX (the Fennel FTS3XX adapter, persisted ID 3) are registered
  through the EF800 static device-plugin
  registry using the stable persisted builder IDs `2` and `3`.
- MainApplication resolves measurement-capable ports, serial defaults, and
  measurement dispatch through the registry while retaining existing UART
  ownership and protocol implementations.
- Production MainApplication owns PluginManager discovery, package lifecycle,
  versioned plugin-ID persistence, Workspace snapshots, Calculation invocation,
  UI sessions, and hardware-capability filtering.
- MainApplication performs startup-only package discovery when internal storage
  is reachable and uses the generic PluginManager opaque descriptor extension
  for EF800 metadata. PluginManager does not interpret EF800 hardware
  requirements.
- External Measurement assignments use version-2 full plugin IDs and are
  filtered by EF800 hardware capabilities before port selection.
- Calculation plugins receive bounded production Workspace snapshots and have
  explicit preview/confirmed append paths without filesystem handles.
- Production MainApplication acceptance assets are under
  `application/ef800-main/tests/`. The former NUCLEO PluginManager prototype
  has been removed; generic PluginManager tests remain in the PluginManager
  repository.

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
- Shared configurable T6963C emulation supports caller-owned 8 KiB or 64 KiB
  RAM profiles, packed one-bit framebuffer rendering, text/graphics composition,
  custom characters, attributes, cursor state, and dirty-region updates.
- Native-sim display tests cover EF800 packed graphics pixels, 64 KiB address
  space, text/custom/attribute/cursor rendering, and suppression of unchanged
  framebuffer state.
- The complete native-sim Twister run passes 24/24 configurations and 147/147
  test cases when reusing the persistent container workspace with `--no-clean`.
- Native-sim PMP upload, atomic virtual-FAT installation, explicit simulator
  restart, package rediscovery, and bounded virtual serial docking are available
  through the local Compose web deployment. Native package execution remains
  behavioral evidence and does not close ARM, MPU, signing, or production
  package-security gates.
- The production `ef800-main` native-sim smoke run reaches `T6963 LCD ready
  (240x64, 40 columns x 8 rows)` and starts the virtual front-panel bridge.
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
- External measurement plugins use full persisted plugin IDs. Legacy Elta3 and
  FTS3XX/FTS3XX assignment IDs 2 and 3 are migrated to NONE and are not
  assignable.
  fixed-point records, and Workspace append behavior.
- Backend-neutral persistence API with safe names and no-storage behavior.
- Generic file-backed target-composition profiles with bounded COMP-Nr. parsing,
  two-slot recovery, unknown-token preservation, repository defaults, and
  restart-scoped activation. The initial catalog is empty and intentionally
  changes no hardware or application behavior.
- Workspace create/select/restore/list/append and Position records.
- Bounded mixed-ending workspace row scanning, row selection, raw-text editing,
  confirmed deletion, journaled replacement/recovery, MTP source-conflict
  detection, and measurement-context recomputation.
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

The current native baseline is 23 Twister scenarios and 133 executed ztest
cases, including QSPI ownership, boot-state NVS, composition, Backup MTP,
virtual USB, and `USBMSD0` coverage. The separate gateway/browser suite has
22 passing unit/asset checks, and the optional black-box MTP round-trip was run
against an isolated simulator using ephemeral flash.

The latest closeout passed pristine MainApplication, BackupApplication,
Bootloader, keyboard-controller, and keyboard-bootloader builds plus image-layout
verification:

| Image | `rom_start` | Binary size | Limit |
|---|---:|---:|---:|
| Bootloader | `0x08000000` | 55,612 B | 65,536 B |
| BackupApplication | `0x08010000` | 103,184 B | 131,072 B |
| MainApplication | `0x08030000` | 260,892 B | 851,968 B |
| Keyboard | `0x08000000` | 22,748 B | 262,144 B |

The image checker was corrected to enforce the specified 128 KiB Backup
partition; its former 96 KiB cap was stale. No physical-target tests or
flashing were performed for this software-only increment. QSPI/FAT geometry,
media migration, rename-metadata power-loss durability, the MTP/local-write
time-of-check race, USB ownership/interruption, and peripheral gates below
remain open.

The production native-sim workspace browser integration test also passed by
launching an isolated simulator with ephemeral flash, uploading mixed-ending
workspace data through the virtual USB/MTP peer, and driving row selection,
editing, cancellation, and deletion through the semantic keyboard/front-panel
protocol:

```powershell
docker exec -w /workspace/application EF800_Devcontainer /opt/venv/bin/python tests/virtual_frontpanel/test_workspace_browser.py
```

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
  signal integrity, and stable reads. Initial EF800 MTP validation succeeded
  after correcting the fitted USB/SPI data-line wiring; full QSPI release
  qualification remains open.
- Reconcile the historical all-zero raw SPI/QSPI/QPI bench result with the
  current driver SFDP/FAT-mount observation; do not claim JEDEC/QSPI
  qualification until measured.
- Validate existing FAT contents without formatting.
- Validate `.configuration` and `data` directories.
- Validate Configuration/Shared Data FAT mounting, approved Main/Backup MTP
  roots, and one-application QSPI ownership across boot selection. Verify any
  optional Main USBMSD restart/ownership transition separately; do not expose a
  FAT volume through raw MSC while an application has it mounted.
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
