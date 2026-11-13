## 1. Establish the Temporary MTP Module

- [x] 1.1 Select an exact commit from upstream Zephyr PR #86832, record its commit ID and provenance, and verify the unmodified class API dependencies against Zephyr v4.4.2.
- [x] 1.2 Create the standalone `ZephyrMTP` Zephyr module with module metadata, Kconfig, CMake integration, public header, MTP class sources, and iterable-section linker fragment while preserving Apache-2.0 attribution.
- [x] 1.3 Add the v4.4.2 compatibility definition for the standard USB Image class without modifying the Zephyr checkout, and add compile-time guards that fail clearly if official conflicting MTP support is present.
- [x] 1.4 Add the module as a pinned west project at `modules/lib/ZephyrMTP`, update the manifest revision, and initialize/update the persistent container workspace only as required by this manifest change.
- [x] 1.5 Add a focused module sample or build test that enables device-next MTP with three storages and proves the module compiles against the pinned Zephyr v4.4.2 workspace.

## 2. Harden MTP Storage Behavior

- [x] 2.1 Configure one MTP instance with independent `Data`, `Plugins`, and `FWU` entries rooted at `/EF800:/data`, `/EF800:/plugins`, and `/EF800:/fwu`, with three storages and 12 handles per storage.
- [x] 2.2 Implement private temporary-file staging for MTP uploads, including collision-resistant names, complete-length verification, close/sync, and atomic promotion to the host-requested final path.
- [x] 2.3 Implement cleanup for cancellation, short writes, protocol errors, USB shutdown, and recognizable orphan MTP temporary files left by power loss without deleting unrelated hidden files.
- [ ] 2.4 Make object-capacity exhaustion fail with a bounded MTP error and diagnostic, and verify that overflow in one storage does not consume or corrupt another storage's object table.
- [ ] 2.5 Add module tests for successful promotion, replacement rollback, cancellation, orphan cleanup, 11/12/13-object boundaries, independent storage capacity, and rebuilding object state on a new session.

## 3. Extend Stable Port Assignment Metadata

- [x] 3.1 Append stable USB MTP builder and connection identifiers without renumbering existing MSD, BlueNRG, I2C, SPI, or CDC values.
- [x] 3.2 Extend port descriptors, capability validation, assignment import/export, display names, and Port 4 menus so MTP is valid only for the intended USB storage builder on Port 4.
- [x] 3.3 Add metadata and port-model tests proving legacy CDC/MSC records retain their meaning, MTP round-trips through current metadata, and invalid MTP assignments are rejected.

## 4. Introduce the Serialized USB Personality Manager

- [x] 4.1 Add a pure transition model for None, USB DebugConsole, USB MTP, and USB MSC that marks only transitions entering or leaving MSC as restart-required.
- [x] 4.2 Add a serialized runtime manager that exclusively owns the physical UDC lifecycle and prevents concurrent CDC, MTP, and MSC contexts.
- [x] 4.3 Refactor USB DebugConsole into a repeatedly startable/stoppable context with one-time descriptor/class registration and explicit blocking of MTP and MSC classes.
- [x] 4.4 Add an MTP context with one-time descriptor/class registration, explicit blocking of CDC and MSC, VBUS-aware activation, and repeatable disable/shutdown/reinitialization.
- [x] 4.5 Implement hot transitions among None, DebugConsole, and MTP with session quiescence, host disconnect interval, re-enumeration, error reporting, and rollback to the prior mode or None.
- [x] 4.6 Implement persistence-first controlled restart for transitions entering or leaving MSC, and prohibit same-boot FAT remount after active MSC.
- [x] 4.7 Route Port 4 assignment changes through the manager, replacing the blanket USB reboot hint with runtime success/failure or the MSC-specific restart message.
- [ ] 4.8 Add native tests for every transition-matrix edge, repeated/concurrent requests, activation and persistence failures, rollback, VBUS-absent selection, and single-context ownership.

## 5. Make Shared FAT Access Safe

- [x] 5.1 Ensure `data`, `plugins`, and `fwu` exist after a successful FAT mount and before MTP activation, without falling back to exposing `/EF800:` when creation fails.
- [x] 5.2 Enable reentrant FatFS, move long-filename workspace from BSS to the compatible stack mode, and raise the USB device thread stack to the measured MTP baseline.
- [ ] 5.3 Audit EF800Application, PluginManager, MCUmgr, update, and persistence filesystem operations for assumptions invalidated by concurrent MTP access, and enforce completed-file and startup-only plugin-consumption boundaries.
- [ ] 5.4 Verify that MSC remains the only raw-disk owner, MTP never invokes disk-sector access directly, and MCUmgr filesystem access remains serialized without being registered as an unintended native USB class.
- [ ] 5.5 Add native filesystem tests for concurrent operations on different files, incomplete-object exclusion, missing-root handling, and safe recovery after simulated transfer failure.

## 6. Preserve Update and Plugin Safety

- [ ] 6.1 Verify the bootloader rejects incomplete, corrupt, oversized, wrong-address, and CRC-mismatched headered `EF800_FW.bin` files before erasing/programming EF800Application.
- [ ] 6.2 Verify EF800Application rejects incomplete, corrupt, oversized, and wrong-target `EF800_KEYBOARD.pkg` files before programming the keyboard controller.
- [ ] 6.3 Add regression tests proving interrupted MTP uploads never replace completed update files and orphan temporary update files are ignored by both update consumers.
- [ ] 6.4 Verify MTP plugin uploads do not alter the running plugin registry and are considered only by the existing validation/discovery path on subsequent startup.

## 7. Build and Behavioral Verification

- [x] 7.1 Run the complete native Twister suite in the reusable devcontainer and update tests and `docs/migration-status.md` if the authoritative case count changes.
- [x] 7.2 Pristine-build bootloader, backup, EF800Application, and keyboard firmware after the Kconfig, CMake, and manifest changes.
- [x] 7.3 Run `application/scripts/verify-images.sh` and inspect map/RAM usage and USB stack high-water evidence to confirm fixed image layout and memory headroom.
- [x] 7.4 Document module provenance, removal procedure when official Zephyr MTP is adopted, host MTP semantics, three-root/12-object limits, MSC restart rules, and recovery diagnostics.

## 8. Physical Windows and Linux Qualification

- [ ] 8.1 On STM32L496 hardware, verify repeated None/DebugConsole/MTP hot switching with VBUS present and absent, including host disconnect/re-enumeration and no EF800Application restart.
- [ ] 8.2 Verify every transition entering or leaving MSC persists the selected mode and performs a controlled restart before changing FAT/raw-disk ownership.
- [ ] 8.3 Qualify Windows native MTP transfers in both directions for 0, 1, 52, 63, 64, 65, multi-kilobyte, and multi-megabyte files, nested directories, 11/12/13 objects, cancellation, unplug, and reconnect.
- [ ] 8.4 Qualify the same matrix on the documented Linux baseline using direct `libmtp` diagnostics and the selected desktop integration, initially Ubuntu GNOME/GVFS unless release documentation selects another baseline.
- [ ] 8.5 Exercise simultaneous host and EF800 access to different completed files, verify new firmware-created files after a new MTP session, and confirm FAT integrity after interrupted transfers and cable removal.
- [ ] 8.6 Interrupt firmware and keyboard package uploads at multiple offsets, then verify no partial file is promoted and neither update consumer programs an incomplete image.
- [x] 8.7 Record physical evidence and remaining release gates in `docs/migration-status.md` without treating native simulation as USB hardware qualification.
