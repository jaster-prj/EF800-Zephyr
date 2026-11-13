## Why

EF800 currently has separate update paths: the bootloader installs EF800Application, while EF800Application can orchestrate keyboard packages. Bootloader and BackupApplication images still require a separate service operation, and there is no coordinated, validated update set for all six firmware images. A single staged FWU set with explicit target identities and controlled progression would make complete device updates reproducible while preserving recovery paths.

## What Changes

- Add a package-based, sequential full-device update workflow for Bootloader, BackupApplication, EF800Application, Keyboard Updater, Keyboard Bootloader, and Keyboard Application.
- Allow MTP to stage the complete update set in FWU; install only validated packages and record progress so reboot or interruption does not cause ambiguous reapplication.
- Extend the existing bootloader EF800Application update and keyboard package workflows to participate in the coordinated update.
- Replace the current policy that restricts bootloader updating to ST-LINK and excludes BackupApplication from automatic programming, while retaining a safe mechanism for updating the bootloader that never erases code while executing from it.
- Define image target identity, format/version compatibility, bounds, integrity validation, install order, retry/cleanup behavior, and per-image completion reporting.
- Use the staged package names `EF800_BOOT.pkg`, `EF800_BACKUP.pkg`, `EF800_APP.pkg`, `EF800_KEYBOARD_UPDATER.pkg`, `EF800_KEYBOARD_BOOT.pkg`, and `EF800_KEYBOARD_APP.pkg`.

## Capabilities

### New Capabilities
- `full-device-firmware-update`: Stage and coordinate a validated update set covering all six EF800 and keyboard firmware images.

### Modified Capabilities
- `ef800-flash-layout`: Permit controlled package-based updates to the bootloader and BackupApplication while preserving fixed addresses, bounds, and recovery guarantees.
- `backup-mtp-recovery`: Define FWU staging for the full update set without exposing raw flash or mounting the shared filesystem concurrently.

## Impact

- EF800 bootloader, EF800Application, and BackupApplication update/storage paths; shared update package validation and metadata.
- EF800Application's keyboard update coordinator and the L431 updater protocol for keyboard images.
- MTP FWU behavior, package-generation/verification tooling, build-image checks, native tests, hardware qualification, and migration/update documentation.
- Existing `ef800-flash-layout` requirements currently reserve bootloader updates for ST-LINK and prohibit automatic BackupApplication replacement; those requirements must be revised explicitly.
