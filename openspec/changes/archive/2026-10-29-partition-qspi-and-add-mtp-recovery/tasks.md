## 1. Define and implement the flash partition contract

- [x] 1.1 Add QSPI partitions for 32 KiB boot-state NVS, 992 KiB configuration FAT, and 3072 KiB shared-data FAT with exact offsets and erase-alignment assertions.
- [x] 1.2 Remove the internal metadata partition and define Bootloader 64 KiB, BackupApplication 128 KiB, and MainApplication 832 KiB regions while preserving MainApplication at `0x08030000`.
- [x] 1.3 Add distinct disk and mount adapters for configuration and shared-data FAT, retaining `.configuration` paths and updating data/plugin/FWU paths.
- [x] 1.4 Implement external-QSPI boot-state NVS access for Bootloader and MainApplication, including default-state recovery for missing/corrupt records and non-destructive behavior when QSPI is unavailable.
- [x] 1.5 Define and test an explicit backup/migration procedure from the existing monolithic FAT volume; detect legacy media and prohibit automatic formatting or in-place repartitioning.

## 2. Update Bootloader storage and image handling

- [x] 2.1 Read and write boot-state NVS from the dedicated external QSPI region before any FAT mount, and preserve the specified MainApplication fallback behavior.
- [x] 2.2 Mount only the shared-data volume for the existing `fwu/EF800_FW.bin` scan, with formatting disabled and no access to the configuration FAT unless needed for explicit migration.
- [x] 2.3 Preserve MainApplication-only automatic installation, validate the target image and integrity before programming, verify the result, and keep second-level Bootloader updates ST-LINK-only.
- [x] 2.4 Add native tests for missing/corrupt/unavailable boot NVS, invalid or interrupted MainApplication updates, and confirmation that no other internal image region is written.

## 3. Move application configuration and composition to the configuration volume

- [x] 3.1 Point composition profile slots, Port 1-5 assignment records, time settings, serial settings, and workspace selection at the dedicated configuration FAT volume.
- [x] 3.2 Remove the internal boot-metadata mirror for application port assignments while retaining only boot-state fields needed by the Bootloader.
- [x] 3.3 Register target-specific `USBMSD0` and `USBMSD1` composition catalog entries and defaults for native-sim and physical EF800 builds.
- [x] 3.4 Filter MainApplication Port 4 choices and runtime behavior by effective composition; automatically clear a stale MSD assignment when `USBMSD0` is active.
- [x] 3.5 Add composition, stale-assignment cleanup, migration, and Main MSD ownership regression tests.

## 4. Implement BackupApplication recovery MTP

- [x] 4.1 Enable the pinned Zephyr MTP class in BackupApplication and register Configuration, Data, Plugins, and FWU as separate MTP storages.
- [x] 4.2 Mount the configuration and shared-data FAT regions only in BackupApplication recovery mode; remove raw MSC ownership of volumes mounted for MTP.
- [x] 4.3 Preserve atomic MTP upload completion and verify interruption, cancellation, disconnect, object-capacity, and filesystem-integrity behavior for each storage.
- [ ] 4.4 Measure BackupApplication image size, RAM, and USB thread stack use against its 128 KiB internal partition.

## 5. Exercise MainApplication CDC/MTP on native-sim

- [x] 5.1 Add the Zephyr device-next virtual UDC/UHC bus to the native-sim profile and enable the required device and host stacks.
- [x] 5.2 Refactor MainApplication CDC/MTP contexts so native-sim uses the real device-next implementations while STM32 clock-recovery remains a physical-only adapter.
- [x] 5.3 Implement an in-process software host peer for CDC enumeration/data tests and the bounded MTP operations used by browser list, download, and upload actions.
- [x] 5.4 Add native-sim tests that enumerate the real MainApplication MTP context and transfer files through virtual USB transactions into the configured FAT storage.
- [x] 5.5 Verify `USBMSD0` excludes MainApplication MSC and leaves shared-data FAT mounted in native-sim.

## 6. Add browser MTP file operations through the gateway

- [x] 6.1 Extend the bounded simulator/gateway protocol with MTP list, upload, download, progress, completion, and failure messages.
- [x] 6.2 Add a browser file view for MainApplication's Data, Plugins, and FWU MTP storages, including drag-and-drop uploads and downloads.
- [x] 6.3 Enforce approved roots, path validation, payload/file-size bounds, transfer cancellation, and no-success-on-partial-transfer behavior at gateway and simulator boundaries.
- [x] 6.4 Add gateway and browser-facing tests proving operations traverse the software USB host and production MTP class rather than direct simulator filesystem APIs.

## 7. Build, document, and close out software verification

- [x] 7.1 Run native Twister tests for QSPI partition ownership, boot-state NVS, composition, Backup MTP, virtual USB, and browser protocol behavior.
- [x] 7.2 Build Bootloader, BackupApplication, MainApplication, and keyboard firmware and verify all internal image addresses and sizes.
- [x] 7.3 Update flash-layout, persistence, MTP/recovery, migration, and Virtual EF800 documentation; explicitly state that physical QSPI qualification and target flashing remain deferred.
- [x] 7.4 Record software verification and unresolved hardware gates in `docs/migration-status.md` without claiming physical qualification.
