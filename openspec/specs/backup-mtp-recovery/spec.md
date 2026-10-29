# backup-mtp-recovery Specification

## Purpose

TBD - Define the purpose of BackupApplication MTP recovery access.

## Requirements

### Requirement: BackupApplication provides MTP recovery access
BackupApplication SHALL provide a device-next MTP personality over the fitted USB device controller while it owns the device. It SHALL expose four distinct logical storages: Configuration, Data, Plugins, and FWU. Configuration SHALL map only to the QSPI configuration FAT region; Data, Plugins, and FWU SHALL map only to their corresponding approved roots on the shared-data FAT region. The external boot-state NVS and raw internal-flash partitions SHALL NOT be exposed as MTP storages.

#### Scenario: Host opens BackupApplication recovery MTP
- **WHEN** a host establishes an MTP session with BackupApplication
- **THEN** it can browse Configuration, Data, Plugins, and FWU without receiving raw sector access to QSPI or internal flash

#### Scenario: Host repairs configuration or data files
- **WHEN** a host uploads, downloads, lists, or removes a file in an exposed recovery storage
- **THEN** the operation is performed through the mounted FAT filesystem for that storage and does not expose NVS records as ordinary files

### Requirement: BackupApplication MTP does not conflict with MainApplication ownership
BackupApplication SHALL be the sole USB and filesystem owner while active. It SHALL NOT start MSC against a FAT region that it has mounted for MTP, and MainApplication SHALL NOT run concurrently with BackupApplication's mounts.

#### Scenario: Backup recovery session is active
- **WHEN** BackupApplication MTP is enabled
- **THEN** only the MTP class is registered for its USB device context and all QSPI filesystem access remains within BackupApplication

#### Scenario: Recovery device shuts down
- **WHEN** BackupApplication is reset or leaves recovery mode
- **THEN** its MTP session is terminated and all open filesystem transfer state is closed before another firmware image accesses QSPI

### Requirement: Recovery MTP stages MainApplication updates without changing the bootloader update policy
BackupApplication SHALL allow complete MainApplication update files to be staged under the FWU root. The Bootloader SHALL apply only its supported MainApplication update file after validating it. BackupApplication MTP SHALL NOT update the second-level Bootloader or automatically install a BackupApplication image.

#### Scenario: MainApplication image is staged
- **WHEN** a host completes an upload of `EF800_FW.bin` into FWU
- **THEN** the completed file remains available for the Bootloader's validated MainApplication update path on a subsequent reset

#### Scenario: Bootloader or BackupApplication image is staged
- **WHEN** a host copies a Bootloader or BackupApplication image into recovery storage
- **THEN** MTP treats it as a file only and no firmware image is programmed automatically

#### Scenario: Incomplete update upload is interrupted
- **WHEN** an MTP update upload is cancelled, disconnected, or incomplete
- **THEN** the incomplete object is not promoted as a completed update file and the Bootloader does not install it
