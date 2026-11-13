# EF800 Zephyr migration status

This file is the current closeout summary for the Mbed OS to Zephyr port.
Detailed subsystem notes and hardware qualifications remain in the linked
documents; this summary does not imply production qualification of unverified
hardware assumptions.

## External SRAM and plugin isolation

- The EF800 board uses a validated 512 KiB FMC SRAM region at `0x60000000`.
  MainApplication validates SRAM before pool use and selects an internal-only
  fallback when validation fails.
- MainApplication external allocations are explicit. The plugin text/rodata
  arenas and two 4 KiB calculation scratch buffers are external-first with
  internal fallbacks. Worker/supervisor stacks, mailbox and syscall staging
  remain internal. See `external-sram-memory-baseline.md`.
- Plugin metadata v4 declares bounded text, rodata, data, BSS, worker-stack,
  mailbox and plugin-owned shared-buffer requirements. The package verifier and
  MainApplication check section sizes, aggregate writable memory, alignment,
  active arena capacities, and worker MPU layout before accepting a descriptor.
- On-target external-SRAM validation, 72 KiB requested pool accounting,
  discovery of three plugin packages, and calculation-plugin execution have
  been observed. The fault-injected internal-only image also completed core
  storage, USB, LCD, keyboard, plugin discovery, memory diagnostic and
  calculation execution checks.
- The normal MainApplication image is flashed at `0x08030000`; target startup
  validates SRAM and discovers all three packages. Investigation found the
  installed ELTA3/FTS3XX descriptor extensions are 84 bytes (metadata v3),
  while the host requires 104-byte metadata v4. The registry previously failed
  to transition an already-registered package to INCOMPATIBLE, leaving zero
  serial defaults marked READY and causing UART setup to fail with `-ENODEV`.
  Diagnostic-state updates now apply to existing entries. Fresh v4 ELTA3,
  FTS3XX, and calculation packages build and verify, but the target's QSPI
  package copies have not yet been replaced; target instrument protocol
  operation therefore remains unverified and the package replacement is a
  prerequisite for that acceptance test. The updated loader was flashed at
  `0x08030000` and now explicitly rejects those v3 packages (`extension
  missing/truncated: size=84`, bind `-ENOTSUP`) rather than attempting UART
  initialization with zero serial settings. MTP package replacement could not
  be performed in this session because the host reports no raw MTP device.
- This evidence is not a complete MPU/fault/timeout/recovery acceptance matrix.
  Treat plugin isolation and SRAM use as bring-up validated, not release
  qualified, until that matrix is run and recorded.

## Regression baseline

- Native regression suite: the latest complete run passed 199/199 cases across
  27/27 configurations (`application/build/twister-closeout-update`). Focused
  suites pass: UI 16/16, RTC 4/4, battery-SOC 14/14, and keyboard-update 11/11.
- All six firmware targets build in the prescribed devcontainer: MainApplication,
  L496 bootloader, BackupApplication, keyboard application, keyboard bootloader,
  and keyboard updater.
- Image layout verifier reports bootloader at `0x08000000`, backup at
  `0x08010000`, and MainApplication at `0x08030000`, within their fixed slots.

## Domain ownership and C quality

- L496 boot handoff is isolated in `application/ef800-bootloader/src/boot_handoff.c`.
- Keyboard matrix GPIO acquisition and pure debounce are split into
  `application/ef800-keyboard/src/keyboard_matrix.c` and
  `shared/keyboard_protocol/keyboard_matrix_debounce.c`; application I2C target
  transport/request-response ownership is in
  `application/ef800-keyboard/src/keyboard_i2c_service.c`. Scan-code framing,
  UART command/session parsing, virtual debug-key state, and typematic repeat
  state are in `application/ef800-keyboard/src/keyboard_uart_protocol.c`; its
  UART, baud, reboot, updater-entry, and matrix-reset effects are adapters in
  the keyboard `main.c`. The keyboard
  bootloader's Cortex-M handoff, bounded I2C/GPIO probe, and image-vector
  validation are in `keyboard_boot_handoff.c`, `keyboard_boot_probe.c`, and
  `keyboard_boot_flash.c` under
  `application/ef800-keyboard-bootloader/src/`.
- Keyboard updater I2C frame transport and response-before-reboot coordination
  are in `application/ef800-keyboard-updater/src/keyboard_updater_transport.c`;
  command/session processing and bounded Flash operations are in
  `keyboard_updater_session.c` and `keyboard_updater_flash.c`, respectively.
  The keyboard application's updater-partition
  command and Flash transaction handling are in `keyboard_app_update.c`.
- MainApplication's bounded raw workspace row editor state and character-editing
  operations are isolated in `application/ef800-main/src/ef800_workspace_row_editor.c`;
  bounded browser state and version-checked row commits are in
  `ef800_workspace_browser.c`. Position-entry input, validation, and append
  orchestration are in `ef800_position_editor.c`; display and navigation remain
  host-owned. Workspace editor/browser tests cover ordering and bounds, late
  position records, stale-version rejection, exact position record persistence,
  invalid input, and persistence failures.
- MainApplication port-assignment runtime operations, persisted-assignment
  reconciliation, startup UART claims, measurement-port discovery, and the
  selected workflow state now have an explicit adapter-backed owner in
  `application/ef800-main/src/ef800_main_port_workflow.{h,c}`. Hardware UART
  readiness, measurement transport setup, and USB-MSD availability remain
  callbacks supplied by `main.c`; menu navigation and plugin execution/recovery
  remain host-owned. The bounded plugin measurement display model is now
  rendered through `ef800_main_display.c`, preserving the host header and clock.
  Shared menu/list pages, workspace browsing and row-editor presentation, and
  row-delete confirmation are also rendered through `ef800_main_display.c`;
  `main.c` continues to select content and own navigation/workflow decisions.
  Port-workflow regression tests exercise measurement-port filtering, stale
  plugin cleanup, UART claim ordering, and transport initialization. Both
  MainApplication targets build; focused ports and UI suites pass 17/17 and
  16/16 respectively.
- The MainApplication ADC battery adapter now owns its readiness, ADC ring,
   calibrated conversion, and native-sim profile input in
   `ef800_main_battery_adc.{h,c}`. LCD battery-icon state/pixel presentation is
   owned by `ef800_main_display`; startup still selects and composes the ADC or
   fuel-gauge reader. Physical/native MainApplication builds and battery-SOC
   tests pass; the UI icon state-change test passes in the focused UI suite.
- RTC field editing now groups its bounded field/input state, numeric/offset
   parsing, range handling, and key transitions in
   `ef800_main_time_editor.{h,c}`. `main.c` retains RTC reads/writes, display,
   runtime refresh, and menu composition. RTC regression coverage now includes
   field advancement, invalid field recovery, offset input, weekday wrap, and
   final DST completion.
- Keyboard package validation, image-kind/partition planning, block transfer,
  CRC verification, and update transaction cleanup are owned by
  `ef800_keyboard_update_service.{h,c}`. `main.c` provides the reset/updater
  entry, storage/USB ownership, keyboard-ready, and application-version
  adapters. The plan tests cover application, bootloader, updater, and invalid
  image kinds; update protocol tests cover framing, bounds, and recovery.
- The MainApplication shell command handlers are grouped in
  `ef800_main_shell.{h,c}`; `main.c` supplies command-specific callbacks and
  composes the shell with runtime services. Startup composition and native
  virtual-platform callbacks remain in `main.c` as platform/application
  adapters; these are composition boundaries rather than separate workflows.
- Remaining keyboard and MainApplication refactors are still in progress; no
  claim is made that the OpenSpec domain-refactor change is complete.
- The complete configured C-quality run passed clang-tidy and Cppcheck for all
  six firmware images, all 26 native-test configurations, and plugin-host
  sources. Cppcheck analyzed 105 image and 17 plugin-host EF800-owned translation
  units and reported zero owned findings in every group. Targeted review fixed
  an updater output-pointer defect, removed actionable test findings, and
  resolved style findings; narrow suppressions document fixed plugin ABI
  callbacks, Kconfig-dependent logging, and test fault-reset flows. Explicit-
  source CScout parsing has
  not yet produced usable function metrics. The combined Cppcheck/CScout gate
  remains open. See `docs/c-code-quality.md` and the refactor change's
  `evidence/analyzer-evaluation.md` for limitations and coverage.

## Outstanding hardware gates

Hardware acceptance was not run as part of this code/build validation. Complete
the L496 boot handoff/update tests, keyboard raw protocol and update-recovery
tests, MPU permissions and denied-access checks, induced plugin fault/timeout/
recovery and resource-cleanup tests, and repeat the plugin acceptance matrix
against the normal non-fault-injected image. Maintain the existing release
gates for QSPI part/geometry qualification, shared FAT/USB ownership, keyboard
electrical/protocol behavior, display timing, oscillator, RTC, and battery
readings. Do not infer hardware acceptance from native tests or successful
cross-builds.
