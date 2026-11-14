# qspi-storage-ownership Specification

## Purpose

TBD - Define QSPI partitioning, ownership, and explicit migration behavior.

## Requirements

### Requirement: QSPI is divided into boot-state, configuration, and shared-data regions
The 4 MiB external QSPI SHALL be divided into a 32 KiB NVS boot-state region at offset `0x000000`, a 992 KiB configuration FAT region at offset `0x008000`, and a 3072 KiB shared-data FAT region at offset `0x100000`. The regions SHALL be erase-aligned, non-overlapping, and cover the QSPI address space exactly.

#### Scenario: Partition map is generated
- **WHEN** the EF800 QSPI devicetree partition map is built
- **THEN** NVS occupies `0x000000..0x007FFF`, configuration occupies `0x008000..0x0FFFFF`, and shared data occupies `0x100000..0x3FFFFF`

#### Scenario: Boot-state NVS contains an invalid record
- **WHEN** the Bootloader can access the boot-state NVS but its record is missing, corrupt, or fails validation
- **THEN** it resets only the boot-state record to defaults and proceeds to a valid EF800 Application without formatting either FAT region

#### Scenario: QSPI or boot-state NVS cannot be accessed
- **WHEN** the Bootloader cannot access the external boot-state NVS
- **THEN** it uses default boot state, reports that the failure counter is unavailable, and attempts a valid EF800 Application without formatting configuration or shared-data storage

### Requirement: Configuration and shared-data ownership are independent
EF800 Application SHALL keep the configuration FAT mounted for application settings and composition profiles while it is running. The shared-data FAT SHALL be mounted by EF800 Application for normal data/plugin/update workflows, or unmounted before EF800 Application USB MSD transfers raw ownership to the host. The two FAT regions SHALL never be represented as one raw MSC disk while EF800 Application continues to mount the shared-data FAT.

#### Scenario: EF800 Application operates with MTP
- **WHEN** EF800 Application selects MTP
- **THEN** it retains filesystem ownership of both configuration and shared-data FAT regions and services MTP operations through filesystem APIs

#### Scenario: EF800 Application hands shared data to MSD
- **WHEN** the effective composition is `USBMSD1` and Port 4 selects USB MSD
- **THEN** EF800 Application preserves configuration access, unmounts only the shared-data FAT before MSC starts, and crosses the existing restart boundary

#### Scenario: EF800 Application does not support MSD in native-sim
- **WHEN** the effective composition is `USBMSD0`
- **THEN** EF800 Application does not start an MSC worker or unmount the shared-data FAT for MSC

### Requirement: EF800 Backup provides exclusive recovery access to QSPI filesystems
EF800 Backup SHALL mount the configuration and shared-data FAT regions for MTP recovery only while it is the active firmware image. It SHALL NOT concurrently expose a raw MSC view of either mounted FAT region.

#### Scenario: EF800 Backup starts recovery MTP
- **WHEN** EF800 Backup starts with usable QSPI volumes
- **THEN** it mounts the configuration and shared-data FAT regions and exposes them through its recovery MTP personality

#### Scenario: EF800 Application and EF800 Backup storage ownership
- **WHEN** either application runs
- **THEN** only that running application accesses the QSPI FAT regions, and the Bootloader is not concurrently using them

### Requirement: Existing QSPI data is migrated explicitly
The system SHALL provide a documented, recoverable migration from the existing single-volume QSPI FAT layout to the three-region layout. Boot or mount failure SHALL NOT automatically format or repartition existing QSPI contents.

#### Scenario: Existing single-volume media is encountered
- **WHEN** firmware configured for the new layout encounters QSPI media that has not been explicitly migrated
- **THEN** it reports the layout mismatch and preserves the original contents without formatting or repartitioning them

#### Scenario: Migration is interrupted
- **WHEN** a migration operation is interrupted before verification completes
- **THEN** the operation reports incomplete migration and does not treat partially copied files as a complete new layout
