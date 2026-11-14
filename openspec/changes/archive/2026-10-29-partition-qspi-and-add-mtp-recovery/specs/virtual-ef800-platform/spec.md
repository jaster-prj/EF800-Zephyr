## ADDED Requirements

### Requirement: Browser file operations exercise EF800 Application MTP
The Virtual EF800 browser SHALL provide a file view for the EF800 Application MTP-visible Data, Plugins, and FWU storages. Browser listing, download, and drag-and-drop upload operations SHALL be performed by an in-process software USB host through the native-sim virtual UHC/bus/UDC and the production EF800 Application MTP class. The browser gateway SHALL NOT implement these operations by directly reading or writing the simulator filesystem.

#### Scenario: Browser lists EF800 Application MTP storages
- **WHEN** a browser opens the virtual EF800 file view and the EF800 Application MTP session is active
- **THEN** it lists Data, Plugins, and FWU using MTP operations and does not expose the private Configuration storage

#### Scenario: Browser uploads a file by drag and drop
- **WHEN** a user drops a file onto an approved EF800 Application MTP storage
- **THEN** the gateway sends the bytes to the in-process host peer, which transfers them through USB MTP into the corresponding FAT root and reports completion only after the MTP operation succeeds

#### Scenario: Browser downloads a file
- **WHEN** a user requests a file from an approved storage
- **THEN** the in-process host peer retrieves it through MTP and the browser receives the completed file without direct filesystem access

#### Scenario: MTP peer or target is unavailable
- **WHEN** the virtual host cannot establish an MTP session or a transfer fails
- **THEN** the browser reports a bounded failure and does not report a partial upload as complete
