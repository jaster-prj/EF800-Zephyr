## MODIFIED Requirements

### Requirement: Bootloader firmware update scope is bounded
The second-level Bootloader SHALL install only validated EF800Application update images from the shared QSPI data volume. Updating the second-level Bootloader SHALL be coordinated by EF800Application using a validated staged package and a controlled mechanism that does not erase or replace code while executing from the bootloader region. BackupApplication image replacement SHALL be performed only by the coordinated update workflow, after validating the staged package and without changing the fixed partition map.

#### Scenario: EF800Application update is installed
- **WHEN** the QSPI data volume contains a complete, supported EF800Application package and the Bootloader validates its target, bounds, vector table, and integrity
- **THEN** the Bootloader installs and verifies the image only in the EF800Application partition

#### Scenario: Bootloader image is present in FWU storage
- **WHEN** a host uploads a valid Bootloader package through MTP and EF800Application coordinates the update
- **THEN** the Bootloader image is validated and installed only through the controlled EF800Application update mechanism, never by the executing Bootloader

#### Scenario: BackupApplication image is present in FWU storage
- **WHEN** a host uploads a valid BackupApplication package through MTP and EF800Application coordinates the update
- **THEN** only the BackupApplication partition is programmed, and the new image is verified before the Bootloader package is applied
