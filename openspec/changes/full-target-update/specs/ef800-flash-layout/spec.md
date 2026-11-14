## MODIFIED Requirements

### Requirement: Bootloader firmware update scope is bounded
The second-level Bootloader SHALL install only validated EF800 Application update images from the shared QSPI data volume. Updating the second-level Bootloader SHALL be coordinated by EF800 Application using a validated staged package and a controlled mechanism that does not erase or replace code while executing from the bootloader region. EF800 Backup image replacement SHALL be performed only by the coordinated update workflow, after validating the staged package and without changing the fixed partition map.

#### Scenario: EF800 Application update is installed
- **WHEN** the QSPI data volume contains a complete, supported EF800 Application package and the Bootloader validates its target, bounds, vector table, and integrity
- **THEN** the Bootloader installs and verifies the image only in the EF800 Application partition

#### Scenario: Initial EF800 Application 1.0.0 bootstrap uses the legacy path
- **WHEN** a device still runs the deployed Bootloader that accepts only `/DATA:/fwu/EF800_FW.bin`
- **THEN** the first versioned EF800 Application image is supplied through that legacy path, and subsequent coordinated EF800 Application updates use `EF800_APP.pkg`

#### Scenario: Bootloader image is present in FWU storage
- **WHEN** a host uploads a valid Bootloader package through MTP
- **THEN** startup coordination leaves it staged; only an explicitly invoked, build-gated EF800 Application shell command may validate and install it, never the executing Bootloader

#### Scenario: EF800 Backup image is present in FWU storage
- **WHEN** a host uploads a valid EF800 Backup package through MTP and EF800 Application coordinates the update
- **THEN** only the EF800 Backup partition is programmed, and the new image is verified before the Bootloader package is applied
