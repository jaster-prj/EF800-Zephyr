# Refactor Implementation Evidence

## L496 bootloader and BackupApplication

- Extracted architecture-specific vector validation at handoff and Cortex-M
  processor cleanup into `application/ef800-bootloader/src/boot_handoff.c`.
  `main()` retains boot composition and policy ordering; update installation,
  boot decision, metadata, and watchdog are called through their existing
  module/service interfaces.
- Added full-partition CRC preservation assertions around rejected update
  packages for Main, Backup, and bootloader partitions. Covered bad CRC,
  truncated payload, and wrong load address in `tests/shared/boot_update`.
- BackupApplication review found that composition, MTP, and LCD behavior are
  already separated. Startup mount preparation, recovery roots, and failure
  unmount cleanup remain together in the startup composition; no additional
  boundary was extracted because this is one lifetime-sensitive sequence.
- Validation on 2026-11-12:
  - `west build -d application/build/ef800-bootloader` passed.
  - `west build -d application/build/ef800-backup` passed.
  - Boot update, metadata, and core tests passed (3 configurations, 17 cases).
  - Boot update tests passed after strengthening rejection checks (1
    configuration, 4 cases).
  - Backup MTP tests passed (1 configuration, 3 cases).
  - `scripts/verify-images.sh` passed: bootloader 61,876/65,536 bytes at
    `0x08000000`; Backup 103,404/131,072 bytes at `0x08010000`; Main
    406,792/851,968 bytes at `0x08030000`; keyboard 29,540/262,144 bytes.

These native and build checks do not replace boot handoff or physical QSPI/USB
hardware acceptance, which remains a closeout gate.

## Keyboard application extraction in progress

- Moved matrix GPIO setup/scanning into
  `application/ef800-keyboard/src/keyboard_matrix.c` and the pure debounce
  transition policy into `shared/keyboard_protocol/keyboard_matrix_debounce.c`.
  The application still owns reporting/repeat behavior and UART command state.
- Moved application I2C target callbacks, bounded updater request capture,
  status/version responses, and response handshaking into
  `application/ef800-keyboard/src/keyboard_i2c_service.c`. Flash transaction
  and updater session logic remain in `main.c` and run in main context.
- Added debounce threshold/make-break and event-capacity atomicity coverage.
  Keyboard application build passed; keyboard application, bootloader, and
  updater builds passed during regression closeout. Native keyboard and
  keyboard-update suites passed: 2 configurations, 17 cases at this increment.
- This is a partial task 3.1 increment only. UART command/repeat ownership,
  updater transaction extraction, and hardware protocol/update acceptance
  remain outstanding; task 3.1 is not marked complete.
- A follow-on task 3.3 slice extracted the keyboard bootloader's
  architecture-specific vector validation and Cortex-M handoff cleanup into
  `application/ef800-keyboard-bootloader/src/keyboard_boot_handoff.c`.
  The bounded I2C/GPIO probe and watchdog-fed window are in
  `keyboard_boot_probe.c`; application/updater vector reads and validation are
  in `keyboard_boot_flash.c`; recovery-policy ordering remains in `main.c`.
  The pristine bootloader build passed while retaining the original
  naked/non-returning branch adapter. Image use is below its 32-KiB partition
  limit (latest 25,276 bytes). Source separation for task 3.3 is complete;
  target timing/watchdog and actual handoff acceptance remain hardware gates.
- A partial task 3.4 slice moved the keyboard updater's bounded I2C transport,
  timeout/overflow metadata, response buffers, busy/ready coordination, and
  response-before-reboot handshaking into
  `application/ef800-keyboard-updater/src/keyboard_updater_transport.c`.
  Command/session and Flash policy stay in the updater composition root. The
  updater was pristine-built (23,776/32,768-byte image) and its native update
  suite passed (1 configuration, 8 cases); task 3.4 remains open for session
  command/Flash extraction and hardware recovery validation.
- Follow-up extraction moved keyboard-application updater-partition command and
  Flash transaction handling into `keyboard_app_update.c`, leaving I2C request
  capture and updater handoff coordination in their existing boundaries. The
  keyboard-updater composition root now delegates command/session processing to
  `keyboard_updater_session.c`; `keyboard_updater_transport.c` remains the
  callback-safe I2C framing boundary, and target-partition-bounded Flash open,
  erase, read, write, and close operations are isolated in
  `keyboard_updater_flash.c`. The updater firmware builds with 23,940/32,768
  bytes of Flash used; keyboard and updater native suites pass (2
  configurations, 17 cases). Physical updater recovery acceptance remains a
  hardware gate.

## Cppcheck and CScout gate follow-up

- Cppcheck was run against explicit EF800 source filters in configured image
  compile databases, with compiler predefined macros captured from the target
  compiler. An actionable `nullPointerRedundantCheck` report in the new updater
  session API exposed a real output-pointer flow issue; the API now rejects null
  output parameters before touching output state. The repeatable runner now
  filters compile databases to EF800-owned translation units, captures target
  compiler built-ins, retains external diagnostics in reports without treating
  them as EF800 findings, and is wired into the CI quality step. The keyboard
  and keyboard-updater configured databases both report zero owned findings.
  Full six-image/native-test CI coverage remains to be exercised.
- The first complete Cppcheck run found actionable issues among the existing
  EF800 backlog. Fixed the test's logically opaque ownership-request expression,
  removed a redundant initialization of the virtual-USB output length, and
  moved `sscanf` out of an `assert` so parsing still occurs when assertions are
  compiled out. Core (9/9) and virtual-USB (3/3) native suites pass after these
  edits; a new complete quality run is underway. Many style suggestions and
  other existing warnings remain and are not silently dispositioned.
- Extracted keyboard scan-code formatting, command/session parsing, typematic
  repeat, and virtual debug-key state into
  `application/ef800-keyboard/src/keyboard_uart_protocol.c`; its mutable command
  and repeat state is module-owned, while UART transmission, baud configuration,
  matrix debounce reset, reboot, and updater entry are explicit platform
  callbacks. Added protocol regression tests for device-ID response,
  typematic-command serialization, and baud-change ACK/configuration ordering.
  The keyboard image builds (30,472/196,608 bytes Flash; 7,360/61,440 bytes RAM),
  the keyboard native suite passes 11/11, and focused Cppcheck reports zero
  owned findings for the keyboard compile database. These checks support
  keyboard tasks 3.1 and 3.2; physical UART/I2C/update acceptance is still a
  separate hardware gate.
- After integrating the UART protocol module into the shared native-test source
  manifest, the complete native suite passed all 26 configurations and 180 test
  cases. The plugin-host suite also passed all five CTest cases after removing
  side effects from assertions. Reviewed and fixed the MainApplication's six
  remaining Cppcheck style findings: const-correct validation, token pointer
  qualification, encoded-buffer scope, and mutable registry lookup that no
  longer casts away the public lookup's const result. MainApplication builds;
  composition, persistence, and plugin native suites pass 47/47, and its
  configured Cppcheck database reports zero owned findings. The all-target
  rerun is in progress; other target/test/package groups remain unreviewed and
  the Cppcheck/CScout CI gate is not yet green.
- Continued the shared/test Cppcheck review: constrained fake-persistence read
  pointers, made workspace and legacy parse pointers const-correct, reduced the
  scope of the workspace row-ending and metadata verification buffers and LCD
  result variable, and simplified the battery failure-count test to inject its
  three consecutive failures directly. Retained two narrowly documented test
  fault-reset suppressions because the test must reset injected faults between
  commit and recovery operations, and two callback-observation suppressions in
  the plugin service test. The affected native configurations pass 76/76 cases;
  MainApplication Cppcheck remains zero. Follow-up Cppcheck review also cleared
  the native-sim USB peer style findings and the measurement plugin host
  findings. Plugin host CTest passes 5/5, and the affected USB, keyboard, and
  LCD native suites pass 24/24. The latest all-target quality rerun passed:
  all six image, 26 native-test, and plugin-host groups passed both focused
  clang-tidy and Cppcheck with zero owned findings. CScout remains blocked by
  GCC/Zephyr preprocessing parser failures, so OpenSpec task 5.4 remains open.
- CScout was built in the reusable devcontainer and scoped to a single EF800
  translation unit. Explicit source selection avoids treating Zephyr source as
  analysis input, but the current CScout parser still fails on GCC/Zephyr
  constructs in the translation unit's preprocessing dependencies and has not
  emitted usable EF800 function metrics. Do not count this as a passing metric
  analysis; task 5.4 remains open while a faithful explicit-source workflow and
  CI baseline are developed.

## Regression refresh

- All six prescribed firmware image targets passed pristine builds. Image
  verification passed with bootloader 61,876/65,536 bytes at `0x08000000`,
  Backup 103,404/131,072 at `0x08010000`, Main 406,792/851,968 at `0x08030000`,
  and keyboard application 30,144/262,144 bytes. Keyboard bootloader uses
  25,276/32,768 bytes and keyboard updater uses 23,776/32,768 bytes. The
  keyboard bootloader was pristine-rebuilt after the probe and flash adapters
  were added.
- The complete native suite passed all 26 configurations and 178 cases.
- After the keyboard updater transport extraction, the complete native suite
  again passed all 26 configurations and 178 cases; the updater-specific suite
  passed all 8 cases. The updated quality runner passed all 101 configured image
  sources, all native-test compile databases, and plugin-host sources with zero
  warnings/errors.
- Hardware acceptance was not run in this session. The outstanding L496 boot
  handoff/update, keyboard protocol/update recovery, plugin MPU/fault/timeout/
  recovery, QSPI/USB ownership, and physical peripheral gates are explicitly
  listed in `docs/migration-status.md`; no hardware pass is inferred from the
  build or native suite.
- `docs/migration-status.md` now records the current module ownership, focused
  C-quality results/profile, regression counts, image sizes/addresses, and
  remaining hardware qualifications.

## MainApplication / external-SRAM sequencing

- Task 4.1 is completed by explicit sequencing, not by claiming the related
  `external-sram-memory-pool` OpenSpec change is complete. That change still has
  pending plugin declaration-validation/integration and target MPU/fault
  acceptance work.
- MainApplication domain moves that relocate persistent state, alter static
  allocation, plugin admission, pool selection, or MPU layout are sequenced
  after those external-SRAM tasks. No memory-pool or plugin-host source was
  moved in this increment. Any future extraction must preserve internal-only
  fallback, plugin memory ownership, and common physical/native source
  composition, and must rerun the applicable pool/plugin checks.

## MainApplication workspace editor increment

- Began task 4.2 with the selected narrow boundary: the MainApplication retains
  display/menu navigation, while `ef800_workspace_row_editor` owns bounded raw
  row text, caret, insert/overwrite mode, and character editing operations. Its
  host still commits through the version-checked, journaled workspace API, so
  row-ending preservation and conflict handling remain with the persistence
  layer. The broader browser/position workflow extraction is not yet complete;
  task 4.2 remains open.
- Added `tests/shared/workspace_editor` for insert/overwrite, caret bounds,
  backspace/delete, row-ending metadata retention, and the 255-byte limit.
  Focused native-sim editor tests pass (1 configuration, 2 cases), and the
  workspace persistence suite passes (1 configuration, 15 cases).
- Added the editor source to the common MainApplication source manifest. Both
  physical and native-sim pristine builds report 51 common sources and pass.
  Native-only build guards were corrected for target-specific STM32 UART
  headers, plugin arena capacity constants, and optional user-worker scratch
  storage; no physical SRAM allocation, fallback, or MPU contract was changed.
