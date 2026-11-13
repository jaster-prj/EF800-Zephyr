## MODIFIED Requirements

### Requirement: Recovery MTP stages EF800Application updates without changing the bootloader update policy
BackupApplication SHALL allow complete, target-identified firmware packages to be staged under the FWU root. BackupApplication MTP SHALL treat all uploaded packages as files only and SHALL NOT program internal flash. On a subsequent controlled boot, the authorized update coordinator SHALL validate and process packages under the full-device update policy. BackupApplication and EF800Application SHALL NOT mount the shared filesystem simultaneously.

#### Scenario: EF800Application image is staged
- **WHEN** a host completes an upload of a valid target-identified firmware package into FWU
- **THEN** the completed file remains available to the authorized coordinator after restart and no image is programmed by BackupApplication

#### Scenario: Bootloader or BackupApplication image is staged
- **WHEN** a host copies any firmware package into recovery storage
- **THEN** MTP treats it as a file only and no firmware image is programmed during the MTP transfer

#### Scenario: Incomplete update upload is interrupted
- **WHEN** an MTP update upload is cancelled, disconnected, or incomplete
- **THEN** the incomplete object is not promoted as a completed update file and the Bootloader does not install it
