## ADDED Requirements

### Requirement: Internal flash contains only the three firmware images
The internal STM32L496 flash layout SHALL reserve the first 64 KiB for the second-level Bootloader, the next 128 KiB for BackupApplication, and the remaining 832 KiB for MainApplication beginning at `0x08030000`. Boot state SHALL NOT require an internal-flash metadata partition.

#### Scenario: Firmware image regions are generated
- **WHEN** firmware partitions are generated for the EF800 target
- **THEN** Bootloader, BackupApplication, and MainApplication occupy non-overlapping regions with the declared addresses and sizes, and MainApplication remains linked at `0x08030000`

#### Scenario: BackupApplication uses the reclaimed metadata region
- **WHEN** the internal image map is built after relocating boot metadata to external QSPI
- **THEN** BackupApplication may use the former 32 KiB metadata region and SHALL remain bounded to its 128 KiB image region

### Requirement: Bootloader firmware update scope is bounded
The second-level Bootloader SHALL install only validated MainApplication update images from the shared QSPI data volume. Updating the second-level Bootloader SHALL remain an ST-LINK service operation; the Bootloader SHALL NOT erase or replace its executing image from an uploaded file. BackupApplication image replacement SHALL remain outside the automatic Bootloader update path.

#### Scenario: MainApplication update is installed
- **WHEN** the QSPI data volume contains a complete, supported `fwu/EF800_FW.bin` image and the Bootloader validates its target, bounds, vector table, and integrity
- **THEN** the Bootloader installs and verifies the image only in the MainApplication partition

#### Scenario: Bootloader image is present in FWU storage
- **WHEN** a host uploads a Bootloader image through MTP
- **THEN** the Bootloader does not install it and the image remains an ordinary staged file until an authorized ST-LINK service procedure is performed

#### Scenario: BackupApplication image is present in FWU storage
- **WHEN** a host uploads a BackupApplication image through MTP
- **THEN** the automatic MainApplication update path does not erase or program the BackupApplication partition
