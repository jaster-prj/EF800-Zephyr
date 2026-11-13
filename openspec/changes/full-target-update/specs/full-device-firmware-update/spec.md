# full-device-firmware-update Specification

## Purpose

Define a staged, package-based update set and coordinator for all six EF800 firmware images.

## ADDED Requirements

### Requirement: Update packages identify and validate one authorized image
Each firmware package SHALL identify exactly one of EF800 Bootloader, BackupApplication, EF800Application, Keyboard Updater, Keyboard Bootloader, or Keyboard Application. Before programming, the installer SHALL validate package format/version, target identity, image size and target bounds, load address, vector table where applicable, and image integrity. It SHALL reject unknown, duplicate, incompatible, malformed, or wrong-target packages without programming them.

#### Scenario: Valid package targets one image
- **WHEN** the coordinator inspects a complete package whose identity, version, bounds, vector, and integrity are valid
- **THEN** it selects only the authorized partition or keyboard update target named by that package

#### Scenario: Package is invalid or targets another device
- **WHEN** a package is malformed, corrupt, incompatible, out of bounds, or identifies an unsupported target
- **THEN** no flash programming begins and the package is reported as rejected

### Requirement: Coordinator processes the FWU set in a safe deterministic order
The update coordinator SHALL process staged packages sequentially in dependency order and persist transaction state across restarts. It SHALL update the EF800 Bootloader last, after EF800Application and BackupApplication are validated. Keyboard packages SHALL be ordered Updater, Keyboard Bootloader, then Keyboard Application.

#### Scenario: Complete compatible set is staged
- **WHEN** all six valid packages are present in FWU and the update coordinator starts
- **THEN** it performs EF800Application activation through the bootloader path, then BackupApplication, then keyboard Updater, Keyboard Bootloader, Keyboard Application, and finally the EF800 Bootloader, recording each verified completion

#### Scenario: Package is absent or an earlier stage fails
- **WHEN** an optional package is absent or a required earlier update fails
- **THEN** the absent target is reported as skipped, dependent targets are not programmed after failure, and transaction status identifies the next safe action

### Requirement: Updating EF800 internal flash preserves explicit partition boundaries
EF800 image programming SHALL use the fixed internal-flash map and SHALL never write outside the partition authorized for the package. A bootloader update initiated by EF800Application SHALL not be installed by the currently executing bootloader; it SHALL use a controlled mechanism that validates the staged image and does not execute code from a region while erasing/programming that region.

#### Scenario: EF800 image is programmed
- **WHEN** a valid EF800 package is installed
- **THEN** writes are limited to its declared partition at the fixed image address and the programmed result is read back and validated

#### Scenario: Bootloader update is interrupted
- **WHEN** power is lost during bootloader replacement
- **THEN** the documented recovery procedure and transaction status identify that ST-LINK recovery may be required, and no other image is misreported as updated

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
