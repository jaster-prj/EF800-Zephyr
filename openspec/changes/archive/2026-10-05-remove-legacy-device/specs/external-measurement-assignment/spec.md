## ADDED Requirements

### Requirement: Persist full external measurement-plugin assignments

The EF800 boot metadata format SHALL use an explicit versioned record containing a full 32-bit plugin ID and connection ID for each port assignment. The encoder, decoder, CRC validation, MainApplication, and bootloader SHALL agree on the new version and record size.

#### Scenario: Version 2 metadata round trip
- **WHEN** valid metadata containing a full external plugin ID is encoded and decoded
- **THEN** all state fields, including every full plugin ID and connection ID, round-trip exactly and CRC validation succeeds

#### Scenario: Bootloader reads version 2 metadata
- **WHEN** the bootloader reads valid version 2 metadata
- **THEN** it preserves boot target/failure behavior and writes any required state changes using the same versioned format before selecting MainApplication or BackupApplication

#### Scenario: Invalid metadata version or size
- **WHEN** metadata has an unsupported version, size, reserved data, or CRC
- **THEN** the consumer rejects it and falls back to initialized safe metadata without interpreting its assignments as valid

### Requirement: Migrate legacy metadata without retaining IDs 2 and 3

The system SHALL decode supported version 1 metadata only for migration. Legacy measurement assignments with IDs `2` and `3` SHALL be discarded and SHALL NOT be remapped to another plugin. Non-measurement system assignments SHALL be migrated only when they remain valid under the new format.

#### Scenario: Legacy Elta3 or FTS3XX assignment is migrated
- **WHEN** version 1 metadata contains assignment ID `2` or `3`
- **THEN** the migrated version 2 metadata contains `NONE` for that port and does not recreate the legacy assignment

#### Scenario: Legacy system assignment is valid
- **WHEN** version 1 metadata contains a valid DebugDevice, USB MSD, or BlueNRG assignment
- **THEN** the assignment is preserved in the version 2 representation with its full plugin/system ID and connection

#### Scenario: Migration is written successfully
- **WHEN** version 1 metadata is read and storage is writable
- **THEN** the system writes version 2 metadata and does not emit version 1 again

### Requirement: Reconcile persisted assignments against loaded measurement plugins

After external plugin discovery, MainApplication SHALL resolve each persisted measurement assignment against the current registry. An assignment SHALL remain active only when its ID identifies a ready loaded measurement plugin with a compatible hardware route and valid connection. Unknown, missing, non-measurement, invalid, or unavailable assignments SHALL be cleared to `NONE`; cleanup SHALL be persisted when storage is writable.

#### Scenario: Assignment matches a ready loaded plugin
- **WHEN** a persisted measurement assignment identifies a ready loaded measurement plugin and a compatible selected port connection
- **THEN** the assignment remains active and MainApplication configures the plugin route

#### Scenario: Assignment plugin is missing
- **WHEN** a persisted assignment ID does not match any currently loaded measurement plugin
- **THEN** MainApplication removes the assignment to `NONE`, does not expose it in the measurement menu, and persists the cleanup when possible

#### Scenario: Assignment identifies a non-measurement plugin
- **WHEN** a persisted assignment ID matches a calculation plugin or another non-measurement registry entry
- **THEN** MainApplication clears the assignment to `NONE` and does not configure a measurement UART for it

#### Scenario: Storage is unavailable during reconciliation
- **WHEN** external plugin discovery or assignment persistence cannot access storage
- **THEN** no external measurement assignment is considered active for that boot, stale assignments are not executed, and the system continues with non-measurement functionality

### Requirement: Expose only loaded external measurement plugins for port assignment

The port-assignment UI SHALL not expose legacy Elta3 or Fennel1/FTS3XX device entries. It SHALL enumerate only ready loaded external measurement plugins whose required hardware capabilities match the selected port, alongside supported non-measurement system assignments.

#### Scenario: External measurement plugin is compatible
- **WHEN** a ready external measurement plugin requires hardware available on the selected port
- **THEN** the plugin appears as an assignable device with its full plugin ID

#### Scenario: Legacy device IDs are selected
- **WHEN** a user or persisted record refers to ID `2` or `3`
- **THEN** the ID is unavailable for new assignment and any persisted occurrence is cleared rather than displayed as a selectable device

### Requirement: Remove the legacy measurement-device dispatch path

MainApplication SHALL configure and execute measurement devices through the external plugin registry and plugin host contract only. The legacy `ef800_device_plugin` assignment, serial-configuration, and measurement-dispatch path SHALL not be used for port assignment or runtime measurement execution.

#### Scenario: External plugin measurement is triggered
- **WHEN** the user measures with an assigned ready external measurement plugin
- **THEN** MainApplication invokes the plugin-host path using the persisted full plugin ID and existing measurement record flow

#### Scenario: Legacy implementation remains in source
- **WHEN** protocol implementation code is retained for packaging or compatibility tests
- **THEN** it is not reachable as a legacy MainApplication assignment or dispatch branch
