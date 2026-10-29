## Why

EF800 currently uses one external QSPI FAT volume for application configuration, user data, plugins, and update files, while MainApplication and BackupApplication alternate between filesystem and raw MSC ownership. This couples private configuration and recovery access to the same volume and leaves BackupApplication unable to offer file-level recovery when MainApplication is unavailable.

## What Changes

- Split the 4 MiB QSPI into a 32 KiB boot-state NVS region, a 992 KiB configuration FAT volume, and a 3072 KiB shared data FAT volume.
- Move the boot metadata NVS from internal flash to external QSPI; keep application configuration and composition profiles in the configuration volume, and remove the assignment mirror from boot metadata.
- Keep the configuration volume application-owned while allowing the shared data volume to switch between MainApplication filesystem/MTP access and restart-gated MSC ownership.
- Add BackupApplication MTP recovery access to Configuration, Data, Plugins, and FWU. Keep the bootloader's automatic firmware installer MainApplication-only; bootloader updates remain ST-LINK-only.
- Make MainApplication's real device-next CDC and MTP contexts exercisable on native-sim through an in-process virtual USB host/device path; expose browser file operations through MTP rather than a direct filesystem shortcut.
- Register composition choices so `USBMSD0` excludes MainApplication MSD in native-sim and `USBMSD1` remains the physical default with MSD selectable on Port 4.
- Revise the internal image map to retain Bootloader, BackupApplication, and MainApplication only, reclaiming the former metadata region while preserving MainApplication's existing linked address. Physical target programming is explicitly out of scope.

## Capabilities

### New Capabilities
- `ef800-flash-layout`: Internal firmware image regions and the external-QSPI boot-state NVS boundary.
- `qspi-storage-ownership`: Partition layout, boot-state NVS, configuration/data volume ownership, and Main/Backup/bootloader access rules.
- `backup-mtp-recovery`: BackupApplication MTP access to the configuration and shared-data roots for recovery and file repair.

### Modified Capabilities
- `target-composition-profile`: Register `USBMSD0`/`USBMSD1` target choices and make USB MSD availability composition-dependent.
- `native-sim-ef800-peripherals`: Provide device-next virtual USB controllers and a software host path for the real MainApplication MTP class.
- `virtual-ef800-platform`: Let the browser exercise MainApplication MTP transfers through that software USB host, with behavioral evidence kept distinct from hardware qualification.
- `virtual-ef800-deployment`: Extend the browser/gateway deployment to drive and observe virtual MTP file operations.

## Impact

- Changes QSPI and internal-flash devicetree partitions, NVS initialization, FAT mount points, MainApplication/BackupApplication USB composition, bootloader update lookup, image-size verification, and target-composition catalog consumers.
- Extends BackupApplication with MTP while preserving its recovery role; MTP-visible configuration access is explicit and does not expose the external boot-state NVS as raw sectors.
- Extends the Virtual EF800 protocol, gateway, and browser UI so native-sim MainApplication MTP is tested through Zephyr's device-next USB stack.
- Requires data migration/recovery rules for the existing single-volume FAT image and native regression/build coverage. Physical QSPI qualification and EF800 target flashing remain later work.
