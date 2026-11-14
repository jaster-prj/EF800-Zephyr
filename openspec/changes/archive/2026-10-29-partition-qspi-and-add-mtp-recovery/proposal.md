## Why

EF800 currently uses one external QSPI FAT volume for application configuration, user data, plugins, and update files, while EF800 Application and EF800 Backup alternate between filesystem and raw MSC ownership. This couples private configuration and recovery access to the same volume and leaves EF800 Backup unable to offer file-level recovery when EF800 Application is unavailable.

## What Changes

- Split the 4 MiB QSPI into a 32 KiB boot-state NVS region, a 992 KiB configuration FAT volume, and a 3072 KiB shared data FAT volume.
- Move the boot metadata NVS from internal flash to external QSPI; keep application configuration and composition profiles in the configuration volume, and remove the assignment mirror from boot metadata.
- Keep the configuration volume application-owned while allowing the shared data volume to switch between EF800 Application filesystem/MTP access and restart-gated MSC ownership.
- Add EF800 Backup MTP recovery access to Configuration, Data, Plugins, and FWU. Keep the bootloader's automatic firmware installer EF800 Application-only; bootloader updates remain ST-LINK-only.
- Make EF800 Application's real device-next CDC and MTP contexts exercisable on native-sim through an in-process virtual USB host/device path; expose browser file operations through MTP rather than a direct filesystem shortcut.
- Register composition choices so `USBMSD0` excludes EF800 Application MSD in native-sim and `USBMSD1` remains the physical default with MSD selectable on Port 4.
- Revise the internal image map to retain Bootloader, EF800 Backup, and EF800 Application only, reclaiming the former metadata region while preserving EF800 Application's existing linked address. Physical target programming is explicitly out of scope.

## Capabilities

### New Capabilities
- `ef800-flash-layout`: Internal firmware image regions and the external-QSPI boot-state NVS boundary.
- `qspi-storage-ownership`: Partition layout, boot-state NVS, configuration/data volume ownership, and Main/Backup/bootloader access rules.
- `backup-mtp-recovery`: EF800 Backup MTP access to the configuration and shared-data roots for recovery and file repair.

### Modified Capabilities
- `target-composition-profile`: Register `USBMSD0`/`USBMSD1` target choices and make USB MSD availability composition-dependent.
- `native-sim-ef800-peripherals`: Provide device-next virtual USB controllers and a software host path for the real EF800 Application MTP class.
- `virtual-ef800-platform`: Let the browser exercise EF800 Application MTP transfers through that software USB host, with behavioral evidence kept distinct from hardware qualification.
- `virtual-ef800-deployment`: Extend the browser/gateway deployment to drive and observe virtual MTP file operations.

## Impact

- Changes QSPI and internal-flash devicetree partitions, NVS initialization, FAT mount points, EF800 Application/EF800 Backup USB composition, bootloader update lookup, image-size verification, and target-composition catalog consumers.
- Extends EF800 Backup with MTP while preserving its recovery role; MTP-visible configuration access is explicit and does not expose the external boot-state NVS as raw sectors.
- Extends the Virtual EF800 protocol, gateway, and browser UI so native-sim EF800 Application MTP is tested through Zephyr's device-next USB stack.
- Requires data migration/recovery rules for the existing single-volume FAT image and native regression/build coverage. Physical QSPI qualification and EF800 target flashing remain later work.
