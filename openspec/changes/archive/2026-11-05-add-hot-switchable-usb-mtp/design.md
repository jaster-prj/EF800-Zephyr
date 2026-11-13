## Context

EF800Application currently uses Zephyr's next-generation USB device stack with separate CDC and MSC contexts. USB MSC requires FAT to be unmounted before the host receives raw disk access, and the MSC worker is not considered safely quiescent for a same-boot FAT remount. Port 4 assignment changes currently display a general reboot hint rather than applying safe USB-only changes at runtime.

Zephyr v4.4.2 does not include MTP, but upstream PR `zephyrproject-rtos/zephyr#86832` implements MTP almost entirely as a self-contained USB class using public device-next and filesystem APIs. EF800 can carry an audited snapshot as a temporary Zephyr module instead of modifying the persistent Zephyr checkout. The module must retain Apache-2.0 provenance and be removable when the selected Zephyr release provides compatible upstream support.

The QSPI FAT volume is mounted at `/EF800:`. EF800Application already creates `data` and `fwu` as needed, package infrastructure uses `plugins`, firmware update uses `/EF800:/fwu/EF800_FW.bin`, and keyboard update uses `/EF800:/fwu/EF800_KEYBOARD.pkg`. The current FatFS configuration uses long-filename BSS storage and is not reentrant. The current USB device thread stack is 1024 bytes, while MTP testing upstream has required a larger stack.

The accepted ADR set contains no supersession chain and does not constrain physical USB personality management. ADR 0008 keeps plugin activation restart-bound for native simulation; the same startup-only package discovery already used by physical EF800Application remains intact rather than introducing plugin hot replacement.

## Goals / Non-Goals

**Goals:**

- Supply a temporary, isolated MTP class compatible with Zephyr v4.4.2.
- Keep FAT mounted while MTP and EF800Application access approved paths through filesystem APIs.
- Present `data`, `plugins`, and `fwu` as three MTP storages with independent 12-object limits.
- Hot-switch safely among None, CDC DebugConsole, and MTP.
- Preserve a reboot boundary for every transition to or from MSC.
- Prevent interrupted MTP uploads from replacing completed files under their final names.
- Preserve stable existing metadata IDs and existing update paths.
- Qualify the behavior on physical STM32L496 hardware with Windows and Linux hosts.

**Non-Goals:**

- Increasing an MTP storage above 12 indexed objects.
- Exposing the volume root, `.configuration`, or arbitrary filesystem paths.
- Simultaneous CDC and MTP interfaces in one composite USB personality.
- Hot-loading or hot-replacing plugins.
- Removing MSC in this increment.
- Treating native_sim as evidence for physical USB interoperability.
- Providing macOS host support.

## Decisions

### Carry MTP as one pinned Zephyr module

Create an independent `ZephyrMTP` west project under `modules/lib/ZephyrMTP`, pinned by commit in `manifest-repo/west.yml`. Its Zephyr module metadata supplies Kconfig, CMake sources, public headers, and the iterable-section linker fragment. Import the MTP class from upstream PR #86832 at a recorded commit, preserve copyright and license headers, and keep EF800 compatibility changes as separately reviewable commits.

The v4.4.2 compatibility layer will define the standard Image USB class value privately because `USB_BCC_IMAGE` is absent in v4.4.2; it will not patch `/workspace/zephyr`. The module will retain upstream-facing Kconfig/API names to minimize eventual migration. A future Zephyr upgrade that contains official MTP must remove the temporary manifest project and compatibility definitions atomically to avoid duplicate symbols and class registrations.

Alternatives considered:

- Fork all of Zephyr: rejected because it broadens the maintained delta and complicates upgrades.
- Copy the class into EF800Application: rejected because it couples reusable protocol code to EF800 application lifecycle code.
- Wait for upstream merge: rejected because the filesystem ownership requirement is current and the PR remains unmerged.

### Use a single serialized EF800 USB personality manager

Introduce one manager that owns desired and active states:

```text
NONE | DEBUG_CDC | MTP | MSC_RESTART_BOUNDARY
```

The manager serializes requests and is the only component allowed to initialize, enable, disable, or shut down the physical UDC contexts. CDC and MTP contexts register descriptors and their one allowed class once, then support repeated `usbd_init()`/`usbd_enable()` cycles after shutdown. Class blocklists prevent MTP, CDC, and MSC from appearing together accidentally.

A non-MSC transition performs:

1. Quiesce the old personality.
2. Disable and shut down its USB context.
3. Leave FAT mounted.
4. Allow a bounded disconnect interval for host recognition.
5. Initialize and enable the new context, or arm it for VBUS.
6. Persist the assignment only after runtime activation succeeds; if persistence fails, roll back where possible.

An activation failure restores the prior non-MSC mode or settles in None and reports the error. Requests arriving during a transition are serialized. MTP shutdown resets its protocol session and transfer state before CDC or None becomes active.

Transitions entering or leaving MSC persist the requested assignment and then perform a controlled restart. On the next boot, existing ownership logic either mounts FAT for None/CDC/MTP or unmounts it before starting MSC. EF800Application never attempts a same-boot MSC-to-FAT handback.

Alternatives considered:

- Extend independent CDC and MSC start functions with pairwise switching: rejected because rollback, persistence, and UDC ownership would remain fragmented.
- Build a composite CDC+MTP device: rejected for the initial release because it adds endpoint and host-enumeration risk while the CP2102/UART4 console remains available.

### Append stable MTP assignment identifiers

Add new MTP builder and connection IDs after existing stable values; do not renumber MSD, BlueNRG, I2C, or SPI. Metadata validation, UI descriptors, assignment import/export, and native tests will recognize the new pair only on Port 4. Existing metadata version 1/2 records retain their current interpretation without migration.

### Register three subdirectories as independent MTP storages

Define one MTP instance with storage entries rooted at:

```text
/EF800:/data
/EF800:/plugins
/EF800:/fwu
```

EF800Application creates missing roots before MTP activation. The module recursively indexes only below each supplied root. Set `CONFIG_USBD_MTP_STORAGES_PER_INSTANCE=3` and `CONFIG_USBD_MTP_MAX_HANDLES=12`; each storage owns an independent object table. The volume root is never registered, so `.configuration` remains unreachable through MTP.

The three storage IDs report the same underlying volume capacity because they share FAT. That host-visible duplication is accepted for the bounded first release. If the imported API cannot provide friendly storage descriptions independently of paths, add a small backward-compatible module field for `Data`, `Plugins`, and `FWU` rather than changing filesystem paths.

Alternatives considered:

- Expose `/EF800:` once: rejected because private content would become visible and directory/file count would exhaust one 12-object table.
- Raise the handle limit now: deferred because Full-Speed responses above 12 handles require a separately qualified multi-packet response design.

### Make MTP uploads atomic at the filesystem boundary

Adapt the temporary module so `SendObjectInfo` opens a hidden, collision-resistant temporary file in the destination directory rather than truncating the final path. The object is promoted with `fs_rename()` only after `SendObject` receives the declared byte count, closes successfully, and synchronizes the file. Cancellation, USB shutdown, protocol errors, and short writes remove the temporary file. Startup/MTP initialization removes recognizable orphan MTP temporary files left by power loss without deleting unrelated dotfiles.

The host-facing object retains its requested final name. Existing completed files are not replaced until promotion succeeds. This protects plugin and update names even when power is lost mid-transfer. Package consumers retain their own format, size, target, vector, and CRC validation; atomic transfer completion is not a substitute for content validation. Plugin discovery remains startup-only.

Alternatives considered:

- Write directly to final names and rely only on update validation: rejected because legacy headerless firmware files and non-update content can leave ambiguous partial objects.
- Add a separate public inbox directory: rejected because the user selected the existing three roots and existing final update paths.

### Enable reentrant FAT with bounded stack changes

Enable `CONFIG_FS_FATFS_REENTRANT` and move long-filename working storage from BSS to stack because Zephyr forbids reentrant FatFS with BSS LFN mode. Increase the USB device thread stack to the qualified MTP value, initially 4096 bytes, and measure stack high-water and image RAM. Filesystem reentrancy serializes FatFS internals; application-level rules still prohibit consuming incomplete objects or hot-replacing loaded plugins.

MCUmgr filesystem access remains another filesystem client and is covered by the same FAT serialization, but tests must verify that MTP personality selection does not accidentally route binary MCUmgr traffic through the native USB DebugConsole context.

### Separate behavioral and hardware verification

Native tests cover the pure personality transition table, metadata compatibility, rollback decisions, storage-root allowlist, object-limit handling, and atomic-upload state decisions. Module build tests compile MTP against Zephyr v4.4.2. Physical acceptance covers actual enumeration and transfers on STM32L496.

The host matrix includes Windows native WPD/Explorer and Linux `libmtp` through the qualified desktop integration. File tests include 0, 1, 52, 63, 64, 65, multi-kilobyte, and multi-megabyte lengths; both directions; nested directories; 11/12/13 objects; cancellation; unplug; reconnect; repeated hot switching; and update files interrupted before completion. FAT integrity and update rejection are checked after fault cases.

## Risks / Trade-offs

- [The upstream PR remains under review and may change] -> Pin one audited commit, record provenance, isolate compatibility patches, and do not track the contributor branch tip.
- [MTP object tables become stale while a host session is open] -> Promise refresh on a new session, not live coherence; reconnect/session reopen is the visibility boundary for firmware-created files.
- [Twelve objects can be exhausted by nested directories or churn] -> Keep three independent roots, reject overflow safely, provide diagnostics, and defer a larger protocol response to a later change.
- [Three logical storages report one physical capacity] -> Accept the duplicate capacity display and document that they share one FAT volume.
- [FatFS reentrancy and LFN stack mode increase stack/RAM use] -> Measure map and stack usage, use explicit stack sizing, and retain image-contract verification.
- [USB shutdown behavior differs across UDC/host combinations] -> Test repeated transitions on the fitted STM32L496 with VBUS present and absent; fall back to None on failure.
- [Power loss can leave temporary files] -> Use a recognizable private temporary-name pattern and bounded orphan cleanup before exposing MTP.
- [Host applications expect a drive letter or POSIX mount] -> Document MTP/WPD/libmtp semantics; MSC remains available through the restart boundary for block-device compatibility.
- [Firmware update validation currently permits legacy headerless images] -> Atomic MTP promotion prevents partial MTP files from taking the final name; retain bootloader validation and separately document that headered CRC images are preferred.

## Migration Plan

1. Add and pin the standalone MTP module; build its sample/class against Zephyr v4.4.2 before EF800Application integration.
2. Add append-only MTP assignment IDs and native metadata/transition tests without changing the current runtime path.
3. Add atomic upload handling and module tests, including cancellation and orphan cleanup.
4. Enable FatFS reentrancy/LFN stack mode and establish memory baselines.
5. Add the USB personality manager and migrate CDC/MTP lifecycle ownership to it while retaining the existing MSC boot boundary.
6. Register the three roots and integrate Port 4 UI/persistence.
7. Run native tests, all four pristine firmware builds, image verification, and module build tests.
8. Qualify MTP and transitions on physical hardware with Windows and Linux, then update migration status and operator documentation.

Rollback removes the MTP assignment from metadata/UI, returns Port 4 runtime handling to the previous CDC/MSC behavior, and removes the manifest module. Existing CDC and MSC persisted IDs remain valid throughout rollback. Media containing `data`, `plugins`, and `fwu` requires no filesystem migration.

## Open Questions

- Select and record the exact upstream PR commit after a build-against-v4.4.2 spike; do not use a moving branch reference.
- Confirm the Linux qualification distribution and desktop integration used for release evidence; Ubuntu with GNOME/GVFS is the proposed baseline, with direct `libmtp` diagnostics retained.
- Confirm through a focused UDC test that repeated shutdown/reinitialization of separate CDC and MTP contexts reliably toggles the STM32 FS pull-up while VBUS remains present.
