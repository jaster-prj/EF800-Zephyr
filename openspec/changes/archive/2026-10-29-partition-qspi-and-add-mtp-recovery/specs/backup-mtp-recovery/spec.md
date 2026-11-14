## ADDED Requirements

### Requirement: EF800 Backup provides MTP recovery access
EF800 Backup SHALL provide a device-next MTP personality over the fitted USB device controller while it owns the device. It SHALL expose four distinct logical storages: Configuration, Data, Plugins, and FWU. Configuration SHALL map only to the QSPI configuration FAT region; Data, Plugins, and FWU SHALL map only to their corresponding approved roots on the shared-data FAT region. The external boot-state NVS and raw internal-flash partitions SHALL NOT be exposed as MTP storages.

#### Scenario: Host opens EF800 Backup recovery MTP
- **WHEN** a host establishes an MTP session with EF800 Backup
- **THEN** it can browse Configuration, Data, Plugins, and FWU without receiving raw sector access to QSPI or internal flash

#### Scenario: Host repairs configuration or data files
- **WHEN** a host uploads, downloads, lists, or removes a file in an exposed recovery storage
- **THEN** the operation is performed through the mounted FAT filesystem for that storage and does not expose NVS records as ordinary files

### Requirement: EF800 Backup MTP does not conflict with EF800 Application ownership
EF800 Backup SHALL be the sole USB and filesystem owner while active. It SHALL NOT start MSC against a FAT region that it has mounted for MTP, and EF800 Application SHALL NOT run concurrently with EF800 Backup's mounts.

#### Scenario: Backup recovery session is active
- **WHEN** EF800 Backup MTP is enabled
- **THEN** only the MTP class is registered for its USB device context and all QSPI filesystem access remains within EF800 Backup

#### Scenario: Recovery device shuts down
- **WHEN** EF800 Backup is reset or leaves recovery mode
- **THEN** its MTP session is terminated and all open filesystem transfer state is closed before another firmware image accesses QSPI

### Requirement: Recovery MTP stages EF800 Application updates without changing the bootloader update policy
EF800 Backup SHALL allow complete EF800 Application update files to be staged under the FWU root. The Bootloader SHALL apply only its supported EF800 Application update file after validating it. EF800 Backup MTP SHALL NOT update the second-level Bootloader or automatically install a EF800 Backup image.

#### Scenario: EF800 Application image is staged
- **WHEN** a host completes an upload of `EF800_FW.bin` into FWU
- **THEN** the completed file remains available for the Bootloader's validated EF800 Application update path on a subsequent reset

#### Scenario: Bootloader or EF800 Backup image is staged
- **WHEN** a host copies a Bootloader or EF800 Backup image into recovery storage
- **THEN** MTP treats it as a file only and no firmware image is programmed automatically

#### Scenario: Incomplete update upload is interrupted
- **WHEN** an MTP update upload is cancelled, disconnected, or incomplete
- **THEN** the incomplete object is not promoted as a completed update file and the Bootloader does not install it
