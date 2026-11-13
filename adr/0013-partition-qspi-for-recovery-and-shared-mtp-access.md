# Partition QSPI for Recovery and Shared MTP Access

- Status: accepted
- Date: 2026-10-28
- Supersedes: None

## Context

EF800 currently exposes one 4 MiB QSPI FAT volume to the Bootloader, EF800Application, and BackupApplication. EF800Application uses the volume for private configuration, workspaces, plugin packages, and update files; BackupApplication exports the whole disk through MSC; and the Bootloader mounts the same volume to find the EF800Application update image. This couples private configuration and boot-state persistence to raw-disk ownership, and BackupApplication cannot provide file-level repair when EF800Application is unavailable.

The accepted MTP decision preserves filesystem-mediated EF800Application access and a restart boundary for MSC. The target-composition profile remains a bounded, file-backed configuration record. The project also uses a fixed internal-flash image map with EF800Application at `0x08030000`; physical QSPI geometry and migration remain hardware qualification gates.

## Decision

Partition the external 4 MiB QSPI into a 32 KiB NVS boot-state region, a 992 KiB configuration FAT region, and a 3072 KiB shared-data FAT region. Store boot-state metadata in the external NVS; keep composition profiles, port assignments, time settings, and workspace selection in the configuration FAT. The shared-data FAT contains `data`, `plugins`, and `fwu`.

Reserve internal flash for the second-level Bootloader (64 KiB), BackupApplication (128 KiB), and EF800Application (832 KiB), preserving the EF800Application link address at `0x08030000`. The Bootloader reads boot state from QSPI NVS, installs only validated EF800Application update images from shared-data `fwu`, and remains ST-LINK-only for its own update. BackupApplication replacement is not part of the automatic update path.

EF800Application always owns the configuration FAT. It owns shared data through FAT/MTP during normal operation; when the physical composition is `USBMSD1` and Port 4 selects MSD, it unmounts only shared data and crosses the existing restart boundary before MSC obtains raw access. The native-sim composition is `USBMSD0`, so EF800Application MSD is not selectable or activated there.

BackupApplication provides recovery MTP over Configuration, Data, Plugins, and FWU while it is the sole running application. EF800Application MTP continues to expose only Data, Plugins, and FWU. Neither MTP personality exports raw sectors or the boot-state NVS.

Native-sim EF800Application uses Zephyr's device-next virtual UDC/UHC bus with the production CDC and MTP contexts. The browser gateway remains the sole connection to the simulator; browser MTP operations are executed by an in-process software host peer through USB transactions, not direct filesystem calls.

## Consequences

- Configuration remains available while EF800Application hands shared-data storage to MSC, eliminating the need to mirror application port assignments in boot metadata.
- Boot-state persistence now depends on external QSPI availability. Invalid state is reset only in its dedicated NVS region; boot proceeds to a valid EF800Application with defaults if state cannot be read, and FAT partitions are never formatted as a recovery fallback.
- The existing monolithic FAT volume requires an explicit backup and migration procedure before QSPI repartitioning; firmware SHALL NOT repartition or format it automatically.
- BackupApplication's 128 KiB image budget must be verified with MTP and filesystem support enabled.
- Native-sim USB tests provide behavioral device-next evidence only; they do not qualify physical enumeration, QSPI behavior, signal integrity, or Windows/Linux interoperability.
- Physical target migration and flashing are deferred until QSPI geometry, migration, and recovery procedures pass their hardware gates.
