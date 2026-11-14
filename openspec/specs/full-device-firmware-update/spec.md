# full-device-firmware-update Specification

## Purpose

Define a staged, package-based update set and coordinator for all six EF800 firmware images.

## Requirements

### Requirement: Update packages identify and validate one authorized image
Each firmware package SHALL identify exactly one of EF800 Bootloader, EF800 Backup, EF800 Application, Keyboard Updater, Keyboard Bootloader, or Keyboard Application. Before programming, the installer SHALL validate package format/version, target identity, image size and target bounds, load address, vector table where applicable, and image integrity. It SHALL reject unknown, duplicate, incompatible, malformed, or wrong-target packages without programming them.

Each of the six images SHALL have version `1.0.0` in this migration. Every subsequent change to an image SHALL increment that image's version. Each package SHALL declare its image semantic version and minimum compatible coordinator version; coordinated packages in this migration SHALL require coordinator version `1.0.0`. The one-time EF800 Application 1.0.0 bootstrap is supplied as the legacy `EF800_FW.bin` accepted by the currently deployed Bootloader; after bootstrap, EF800 Application updates use `EF800_APP.pkg`.

#### Scenario: Valid package targets one image
- **WHEN** the coordinator inspects a complete package whose identity, version, bounds, vector, and integrity are valid
- **THEN** it selects only the authorized partition or keyboard update target named by that package

#### Scenario: Package is invalid or targets another device
- **WHEN** a package is malformed, corrupt, incompatible, out of bounds, or identifies an unsupported target
- **THEN** no flash programming begins and the package is reported as rejected

### Requirement: Coordinator processes the FWU set in a safe deterministic order
The update coordinator SHALL process staged packages sequentially in dependency order and persist transaction state across restarts. It SHALL process EF800 Application, EF800 Backup, and keyboard packages in dependency order. Keyboard packages SHALL be ordered Updater, Keyboard Bootloader, then Keyboard Application. EF800 Bootloader SHALL NOT be programmed automatically by startup coordination; a separate explicit shell command is required.

The transaction journal SHALL use a separate versioned, integrity-checked NVS record in the existing QSPI boot-state partition and SHALL NOT change the fixed flash or QSPI partition maps.

#### Scenario: Complete compatible set is staged
- **WHEN** all six valid packages are present in FWU and the update coordinator starts
- **THEN** it performs EF800 Application activation through the EF800 Bootloader path, then EF800 Backup, then keyboard Updater, Keyboard Bootloader, and Keyboard Application, while leaving the EF800 Bootloader package staged for explicit shell authorization

#### Scenario: Package is absent or an earlier stage fails
- **WHEN** an optional package is absent or a required earlier update fails
- **THEN** the absent target is reported as skipped, dependent targets are not programmed after failure, and transaction status identifies the next safe action

#### Scenario: EF800 Application package remains after bootloader inspection
- **WHEN** `EF800_APP.pkg` is present when EF800 Application starts
- **THEN** EF800 Application validates the package and compares its declared version with its own running version; a mismatch holds all later updates, while a match permits coordination to continue

#### Scenario: EF800 Application package is absent
- **WHEN** EF800 Application starts and `EF800_APP.pkg` is absent
- **THEN** the Application package stage is reported as skipped and the coordinator proceeds to EF800 Backup and later targets

### Requirement: Updating EF800 internal flash preserves explicit partition boundaries
EF800 image programming SHALL use the fixed internal-flash map and SHALL never write outside the partition authorized for the package. A bootloader update initiated by EF800 Application SHALL not be installed by the currently executing bootloader; it SHALL use a controlled mechanism that validates the staged image and does not execute code from a region while erasing/programming that region.

#### Scenario: EF800 image is programmed
- **WHEN** a valid EF800 package is installed
- **THEN** writes are limited to its declared partition at the fixed image address and the programmed result is read back and validated

#### Scenario: Bootloader update is interrupted
- **WHEN** power is lost during bootloader replacement
- **THEN** the documented recovery procedure and transaction status identify that ST-LINK recovery may be required, and no other image is misreported as updated

#### Scenario: Bootloader update requires an explicit shell gate
- **WHEN** `EF800_BOOT.pkg` is staged but the user has not invoked `bootloader_update`
- **THEN** startup coordination does not erase or program the Bootloader partition

#### Scenario: User manually triggers a valid Bootloader package
- **WHEN** the build-time Bootloader-update gate is enabled, shared storage is exclusively owned, and the user invokes `bootloader_update`
- **THEN** EF800 Application validates the exact Bootloader package, writes only the fixed Bootloader partition, verifies the programmed image, and removes the package only after verified completion

### Requirement: Update packages remain staged until verified completion
The coordinator SHALL keep each package available until the corresponding programmed image passes readback/integrity and bootability checks. It SHALL remove or archive only the package whose completion is verified, and SHALL provide a per-target result for success, rejection, skip, or retry-required failure.

#### Scenario: Image passes post-program verification
- **WHEN** the complete image is programmed and its readback, integrity, and applicable vector checks pass
- **THEN** that target is marked complete and only its package may be cleaned up

#### Scenario: Programming or verification fails
- **WHEN** programming is interrupted or post-program checks fail
- **THEN** the target is not marked complete and its package remains available for diagnosis or retry

### Requirement: Update staging does not expose raw flash
USB MTP SHALL expose the FWU filesystem for package staging only. MTP SHALL NOT expose internal flash partitions or provide host-directed raw flash writes, and the update coordinator SHALL retain sole ownership of firmware programming.

#### Scenario: Host stages a complete update set
- **WHEN** a host transfers one or more complete packages into FWU
- **THEN** the files become available to the on-device coordinator without exposing raw internal flash

#### Scenario: Host attempts to write firmware sectors through MTP
- **WHEN** a host requests access outside approved filesystem paths
- **THEN** the request is denied and no flash partition is modified
