# EF800 Firmware Ownership and Dependency Map

Baseline reviewed: 2026-11-12. This captures the current pre-refactor structure,
state ownership, dependency direction, resource lifetime hotspots, and source
manifest boundaries. Line ranges are navigation aids, not proposed module
boundaries. The CMake manifests remain the authority for source membership.

Task 2.1 increment: architecture-specific L496 handoff now lives in
`application/ef800-bootloader/src/boot_handoff.c` behind
`ef800_boot_handoff()`. Decision selection, update installation, metadata,
watchdog, and handoff remain separate modules/services coordinated by boot
`main()`. Startup order and recovery policy were left in the composition root.

Task 2.3 review: EF800 Backup already has cohesive composition and MTP
adapter modules. Its remaining entry-point responsibilities are ordered startup,
exclusive no-format configuration/data mounts, recovery-directory preparation,
LCD initialization, and startup-failure unmount cleanup. These operations share
one startup lifetime and have existing MTP integration coverage; no additional
module boundary was justified by this review, so no Backup code was moved.

## Image composition and source dependencies

| Image | Composition root and source composition | Main dependency direction |
|---|---|---|
| EF800 Application (L496) | `application/ef800-application/src/main.c` (7,771 lines) plus the common manifest `application/ef800-application/ef800-application-sources.cmake` and physical/native platform adapters. | `main.c` owns startup/UI workflow and binds the shared policy/domain modules: persistence/workspace, position, ports/UART registry, measurement/plugin host, keyboard host/update client, composition, USB/storage, RTC/battery, LCD/UI, and virtual adapters. Physical/native-sim use the same common production manifest; platform-specific adapters are appended by CMake. Plugin package behavior remains in `application/plugins/measurement-plugin` and its ABI/shared worker SDK; host owns hardware and session lifetime. |
| L496 second-level bootloader | `application/ef800-bootloader/src/main.c` (313 lines), `boot_update.c`, early/reset watchdog adapters, and shared boot decision, update/metadata, console/log, watchdog, and QSPI layout modules. | Entry point reads flash/vector state and metadata, asks shared decision/update policy, coordinates watchdog and recovery, then uses the local CMSIS handoff adapter. QSPI partition constants and linker/device-tree contracts are checked with build assertions. |
| EF800 Backup | `application/ef800-backup/src/main.c` (206 lines), `ef800_backup_composition.c`, `ef800_backup_usb_mtp.c`, and shared composition, storage/persistence, watchdog, LCD contrast, console/log, and update modules. | Entry point owns recovery startup and filesystem mounts; shared QSPI-layout/composition policy is consulted before mounts; MTP transport is isolated in the Backup adapter and ZephyrMTP dependency. It is the exclusive Backup USB/filesystem owner while active. |
| Keyboard application (L431) | `application/keyboard-application/src/{main,keyboard_matrix,keyboard_i2c_service,keyboard_key_names}.c`; shared keyboard translation and matrix-debounce, boot policy, update protocol, update, and watchdog modules. | Matrix GPIO scan and debounce now have separate modules; I2C status/update transport and bounded request-response handoff are in `keyboard_i2c_service.c`. UART command/repeat/reporting and updater-only flash transaction/session remain in `main.c`; shared modules hold reusable protocol, policy, and package validation. |
| Keyboard bootloader (L431) | `application/keyboard-bootloader/src/{main,keyboard_boot_handoff,keyboard_boot_probe,keyboard_boot_flash}.c`, shared keyboard boot policy, update/vector validation, and watchdog modules. | `keyboard_boot_probe.c` owns the bounded I2C probe and GPIO request adapters; `keyboard_boot_flash.c` owns application/updater vector reads and validation; entry point owns recovery orchestration; `keyboard_boot_handoff.c` owns Cortex-M cleanup/branch; shared policy decides updater-entry/recovery. |
| Keyboard updater (L431) | `application/keyboard-updater/src/{main,keyboard_updater_transport}.c`, shared keyboard update framing/validation, update, and watchdog modules. | `keyboard_updater_transport.c` owns bounded I2C frame collection, response transport, and response-before-reboot handshaking; main context retains version/command processing, session state, partition checks, flash operations, and integrity checks. |

Detailed target source lists and ownership exclusions are in
`source-inventory.md`. Native tests are selected by `tests/shared/*` CMake and
Twister manifests; plugin host/package inputs are separately selected by
`application/plugins/measurement-plugin/CMakeLists.txt`.

## State and resource ownership hotspots

### EF800 Application

- `main.c:92-194` contains static device handles, keyboard UART queue/mutex/
  semaphore and request flags, board driver instances, persistence/composition,
  workspace/assignment/UART/plugin registries, measurement selection/result,
  UI/menu state, battery/RTC service readiness, and asynchronous plugin UI,
  display-cache, failure, and workspace-snapshot state. These are the strongest
  candidates for domain-owned contexts; avoid moving the whole set into one
  replacement global context.
- Approximate behavior clusters: keyboard package upload/update client
  `1270-1980`; display/settings/workspace view and editing `2069-2918`; port
  assignment `2919-3308`; measurement/plugin/calculation workflow
  `3309-4294`; navigation and workspace selection `4295-5145`; menu/application
  key handling `5146-5749`; battery/RTC and diagnostics `5750-6337`; remaining
  console, startup, and platform integration follow. Exact boundaries should
  be confirmed from function ownership before extraction.
- Hardware devices and service instances are static for image lifetime. UART
  ISR input crosses through a bounded message queue; command serialization uses
  a mutex/semaphore and state flags. Plugin UART claims, worker/session, and
  async results must be reconciled and torn down on assignment change, submenu
  exit, and failure. Workspace persistence is routed through the shared
  persistence/journal APIs. EF800 Application QSPI FAT mounting remains gated by
  the existing QSPI ownership decision; do not create a competing mount.

### L496 bootloader and EF800 Backup

- L496 bootloader's static state is mostly local to `main`: opened Main/Backup
  flash areas, metadata and validity/dirty flags, boot-target decisions, and
  watchdog/reset observations. `main.c:85-167` provides vector and low-level
  handoff helpers; `main.c:169-313` coordinates startup. Update package handling
  is behind `boot_update.c` and shared update policy. Flash-area handles are
  acquired during the boot decision and retained through selection/handoff;
  verify cleanup expectations when reorganizing, without changing startup order.
- Backup owns static FATFS instances and mount descriptors for the
  configuration and shared-data partitions (`main.c:28-45`). Mounts are
  `NO_FORMAT`; the entry point checks USB/QSPI/layout prerequisites before
  mounting. If subsequent startup fails, it unmounts whichever filesystems were
  mounted; successful mounts live while recovery MTP is active. The watchdog
  thread has static stack/thread storage and runs for the application lifetime.
  Never overlap Backup MTP ownership with EF800 Application mounts.

### Keyboard application family

- Keyboard application static state groups naturally into: GPIO matrix
  descriptors and scan/debounce/repeat state; UART RX queue and protocol
  command parser; I2C target response/command transport; updater request/response
  buffers and transaction flags; updater session (image size, CRC, progress,
  erased-page bitmap); flash scratch/write buffers. Matrix GPIO acquisition is
  now in `keyboard_matrix.c`, pure debounce state transitions in
  `shared/keyboard_protocol/keyboard_matrix_debounce.c`, and I2C target
  callbacks/request-response handoff in `keyboard_i2c_service.c`. UART RX,
  command/repeat behavior and updater flash/session transaction logic still
  reside in `main.c`; these remain explicit extraction work, not completed
  boundaries. Preserve callback serialization and response timing.
- `main.c:134-287` handles I2C service transport; `343-603` handles the
  updater-partition application transaction; `604-687` owns target
  initialization and verified updater handoff; `688-974` handles key repeat,
  matrix state, and change reporting; `975-1206` handles command parsing,
  UART ISR, and hardware initialization; `1230+` is startup orchestration.
  Flash areas are opened only around transaction operations and closed on
  their completion/error paths. UART queue and static session/buffers persist
  for image lifetime.
 - Keyboard bootloader owns the bounded I2C/GPIO probe and watchdog window.
   Architecture-specific vector validation and Cortex-M interrupt/timer cleanup
    and branch now live in `keyboard_boot_handoff.c`; probe/GPIO adapters and
    watchdog-fed bounded timing now live in `keyboard_boot_probe.c`; image
    vector reads/validation live in `keyboard_boot_flash.c`. Before entering
    updater, the composition root unregisters the target and invokes the handoff
    adapter. Recovery-policy ordering remains in `main.c`; preserve the
    no-package-write contract.
- Keyboard updater owns a bounded request frame, response buffers, atomics for
  request/response/reboot coordination, and a session with target, size, CRC,
  received bytes, validation state, and erased-page bitmap. I2C callbacks only
  transport bytes; `process_request` and command handlers perform policy and
  flash work in main context. `target_area` opens the selected permitted
  partition; operation paths close the flash-area handle. Reboot waits for the
  response path as specified.

Task 3.4 increment: bounded I2C request/response transport, timeout/overflow
metadata handoff, busy/response-ready coordination, and reboot-after-response
are now owned by `application/keyboard-updater/src/keyboard_updater_transport.c`.
Session command processing, target authorization and bounded Flash operations
remain in `main.c`; keep those as the next extraction boundary.

## Pre-refactor hotspots to review first

1. EF800 Application has the broadest cross-domain mutable state and the longest
   workflow function surface. Prioritize ownership of workspace/editor state,
   assignment/UART claims, measurement/plugin lifecycle, keyboard update,
   display/navigation, and board adapters in separate increments.
2. The keyboard application and updater each combine I2C callback transport
   with session/update state. Keep callback-shared buffers and transaction
   state explicit; do not move flash work into callback context.
3. L496 bootloader startup order crosses watchdog, metadata, update install,
   image validation, and architecture handoff. Refactor at pure policy and
   adapter seams without changing recovery precedence or flash authorization.
4. Backup startup is already split across MTP/composition modules; preserve its
   current mount/cleanup ownership and extract only a demonstrated missing
   boundary.
5. Resource-sensitive flows are UART/worker claims, filesystem mounts and
   staged writes, flash-area handles, watchdog thread, and I2C target callback
   registration. Each extraction should list acquisition, owner, release, and
   failure cleanup before code moves.
