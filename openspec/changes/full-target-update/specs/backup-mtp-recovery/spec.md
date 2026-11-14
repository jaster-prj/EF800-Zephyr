## MODIFIED Requirements

### Requirement: Recovery MTP stages EF800 Application updates without changing the bootloader update policy
EF800 Backup SHALL allow complete, target-identified firmware packages to be staged under the FWU root. EF800 Backup MTP SHALL treat all uploaded packages as files only and SHALL NOT program internal flash. On a subsequent controlled boot, the authorized update coordinator SHALL validate and process packages under the full-device update policy. EF800 Backup and EF800 Application SHALL NOT mount the shared filesystem simultaneously.

#### Scenario: EF800 Application image is staged
- **WHEN** a host completes an upload of a valid target-identified firmware package into FWU
- **THEN** the completed file remains available to the authorized coordinator after restart and no image is programmed by EF800 Backup

#### Scenario: One-time EF800 Application bootstrap image is staged
- **WHEN** a host stages the legacy `EF800_FW.bin` bootstrap image for a device whose deployed Bootloader does not support the package envelope
- **THEN** EF800 Backup treats it as a file only, and the currently deployed Bootloader may install it only through its existing controlled update path

#### Scenario: EF800 Bootloader or EF800 Backup image is staged
- **WHEN** a host copies any firmware package into recovery storage
- **THEN** MTP treats it as a file only and no firmware image is programmed during the MTP transfer

#### Scenario: Incomplete update upload is interrupted
- **WHEN** an MTP update upload is cancelled, disconnected, or incomplete
- **THEN** the incomplete object is not promoted as a completed update file and the Bootloader does not install it
