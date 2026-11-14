## Why

EF800 currently has separate update paths: the bootloader installs EF800 Application, while EF800 Application can orchestrate keyboard packages. EF800 Bootloader and EF800 Backup images still require a separate service operation, and there is no coordinated, validated update set for all six firmware images. A single staged FWU set with explicit target identities and controlled progression would make complete device updates reproducible while preserving recovery paths.

## What Changes

- Add a package-based, sequential full-device update workflow for EF800 Bootloader, EF800 Backup, EF800 Application, Keyboard Updater, Keyboard Bootloader, and Keyboard Application.
- Allow MTP to stage the complete update set in FWU; install only validated packages and record progress so reboot or interruption does not cause ambiguous reapplication.
- Extend the existing bootloader EF800 Application update and keyboard package workflows to participate in the coordinated update.
- Replace the current policy that restricts EF800 Bootloader updating to ST-LINK and excludes EF800 Backup from automatic programming. Keep Bootloader installation behind a build-time gate, an exact staged package, and an explicit EF800 Application shell command; never install it automatically at startup.
- Define image target identity, format/version compatibility, bounds, integrity validation, install order, retry/cleanup behavior, and per-image completion reporting.
- Use the staged package names `EF800_BOOT.pkg`, `EF800_BACKUP.pkg`, `EF800_APP.pkg`, `EF800_KEYBOARD_UPDATER.pkg`, `EF800_KEYBOARD_BOOT.pkg`, and `EF800_KEYBOARD_APP.pkg`.
- Bootstrap the first versioned EF800 Application image once through the currently deployed Bootloader's legacy `EF800_FW.bin` path. After this transition, EF800 Application updates use `EF800_APP.pkg`.
- Assign version `1.0.0` to each of the six images in this change. Every later change to an image SHALL increment that image's version.

## Capabilities

### New Capabilities
- `full-device-firmware-update`: Stage and coordinate a validated update set covering all six EF800 and keyboard firmware images.

### Modified Capabilities
- `ef800-flash-layout`: Permit controlled package-based updates to the EF800 Bootloader and EF800 Backup while preserving fixed addresses, bounds, and recovery guarantees.
- `backup-mtp-recovery`: Define FWU staging for the full update set without exposing raw flash or mounting the shared filesystem concurrently.

## Impact

- EF800 Bootloader, EF800 Application, and EF800 Backup update/storage paths; shared update package validation and metadata.
- EF800 Application's keyboard update coordinator and the L431 updater protocol for keyboard images.
- MTP FWU behavior, package-generation/verification tooling, build-image checks, native tests, hardware qualification, and migration/update documentation.
- Existing `ef800-flash-layout` requirements currently reserve bootloader updates for ST-LINK and prohibit automatic EF800 Backup replacement; those requirements must be revised explicitly.
