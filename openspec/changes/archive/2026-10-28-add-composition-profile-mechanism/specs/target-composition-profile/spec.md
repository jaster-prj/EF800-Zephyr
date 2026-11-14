## ADDED Requirements

### Requirement: Persist a bounded target composition profile
The system SHALL represent the target profile as a versioned, integrity-protected configuration record containing a generation and exactly one `composition=` field whose value is a space-separated list of COMP-Nr. tokens. A COMP-Nr. token SHALL use a bounded, documented ASCII syntax, and the parser SHALL reject duplicate tokens, duplicate fields, oversized input, malformed metadata, and failed integrity checks without partially accepting the record.

#### Scenario: Valid profile is loaded
- **WHEN** the configuration storage contains a valid supported target profile with a bounded composition list
- **THEN** the system loads the complete stored token list and its generation

#### Scenario: Empty composition is loaded
- **WHEN** a valid profile contains an empty `composition=` value
- **THEN** the system accepts it as an explicitly empty stored composition

#### Scenario: Malformed profile is encountered
- **WHEN** the target-profile record is malformed, oversized, duplicated, unsupported in format, or fails its integrity check
- **THEN** the system rejects the complete record, reports the reason, and does not activate a partial composition

### Requirement: Resolve omitted selections through repository defaults
The system SHALL resolve stored COMP-Nr. tokens through a repository-owned catalog interface and SHALL apply the registered default for each catalog group that has no recognized stored selection. A missing profile, missing composition field in legacy or unavailable input, empty composition, or unavailable configuration storage SHALL preserve current application behavior through defaults rather than preventing startup.

#### Scenario: Registered group selection is omitted
- **WHEN** a catalog group has a registered default and the stored composition has no recognized selection for that group
- **THEN** the effective composition contains that registered default

#### Scenario: No catalog entries exist
- **WHEN** the initial mechanism has no hardware-specific catalog entries or consumers
- **THEN** the effective composition is empty and existing EF800 runtime behavior remains unchanged

#### Scenario: Configuration storage is unavailable
- **WHEN** EF800 Application cannot read configuration storage during startup
- **THEN** composition initialization completes with defaults and reports that no persisted profile was activated

### Requirement: Ignore and preserve unknown COMP-Nr. tokens
The system SHALL retain syntactically valid COMP-Nr. tokens that the current catalog does not recognize, SHALL exclude them from the effective composition, and SHALL preserve them across service-managed profile updates unless the caller explicitly removes them. Unknown tokens SHALL NOT activate hardware, software functions, ports, menus, plugins, or pin configuration.

#### Scenario: Newer token is read by older firmware
- **WHEN** a valid profile contains a syntactically valid COMP-Nr. that is absent from the current firmware catalog
- **THEN** the system reports it as unknown, preserves it in the stored snapshot, and leaves the effective composition unchanged by that token

#### Scenario: Known selection is changed alongside an unknown token
- **WHEN** the service updates a recognized selection while the stored profile also contains an unrelated unknown token
- **THEN** the committed profile retains the unknown token exactly once

### Requirement: Expose stored, effective, and pending composition state
The composition service SHALL expose bounded immutable snapshots that distinguish the raw stored token set, the catalog-resolved effective selections and defaults, unknown tokens, active generation, and any staged pending profile. Callers SHALL NOT need to parse the persisted file directly.

#### Scenario: Caller inspects composition state
- **WHEN** a caller requests the composition snapshot after initialization
- **THEN** it receives separately identifiable stored, effective, defaulted, and unknown state within published bounds

#### Scenario: Pending profile exists
- **WHEN** a validated modification has been staged but not committed
- **THEN** the service reports the pending profile separately and leaves the active snapshot unchanged

### Requirement: Modify composition through a validated staged operation
The composition service SHALL support bounded add, remove, and complete-replacement operations against a pending token set, SHALL validate syntax, uniqueness, catalog constraints known to the current firmware, and encoded size before persistence, and SHALL leave the active composition unchanged when staging or validation fails.

#### Scenario: Token is staged successfully
- **WHEN** a caller stages a syntactically valid token change that satisfies current catalog constraints
- **THEN** the pending profile reflects the change while the active composition remains unchanged

#### Scenario: Invalid modification is requested
- **WHEN** a caller stages a malformed, duplicate, oversized, or catalog-conflicting composition
- **THEN** the service rejects the request without changing the pending or active profile

#### Scenario: Pending modification is discarded
- **WHEN** a caller discards a pending profile
- **THEN** the service removes the pending state and retains the active stored and effective snapshots

### Requirement: Commit profile changes with recovery and verification
The composition service SHALL persist a committed profile with an incremented generation, SHALL verify the encoded record by reading and decoding it, and SHALL retain a previously valid record when a write, verification, or interrupted replacement fails. Startup SHALL select the newest complete valid generation and SHALL ignore incomplete or corrupt candidates.

#### Scenario: Profile commit succeeds
- **WHEN** a valid pending profile is committed to available configuration storage
- **THEN** the service writes an incremented generation, verifies the complete record, and reports that the committed profile requires restart activation

#### Scenario: Profile commit is interrupted
- **WHEN** power loss or a storage failure interrupts profile replacement
- **THEN** the next initialization selects the newest complete valid generation or the previous valid generation and never activates a partial record

#### Scenario: Read-back verification fails
- **WHEN** the newly written profile cannot be read back or does not decode to the staged profile
- **THEN** the commit fails and the previous valid profile remains eligible for activation

### Requirement: Activate composition only during initialization
The system SHALL activate a persisted composition only when the composition service is initialized. Committing a changed profile SHALL NOT reconfigure running hardware, alter functions, modify assignments, or change the current effective snapshot; it SHALL report restart-required state until the next initialization.

#### Scenario: Profile is committed during a boot
- **WHEN** a pending profile is committed successfully while EF800 Application is running
- **THEN** the active effective composition remains unchanged and restart-required state is reported

#### Scenario: Application initializes after commit
- **WHEN** EF800 Application next initializes the composition service and the committed generation is valid
- **THEN** that generation becomes the active stored profile and is resolved through the current catalog

### Requirement: Keep the initial mechanism composition-neutral
The initial composition mechanism SHALL NOT define meanings for example COMP-Nr. values and SHALL NOT change LCD, Bluetooth, battery, port, plugin-assignment, menu, pin-control, DeviceTree, or other target behavior based on composition. Hardware-specific catalog entries and consumers SHALL be introduced only by later specified changes.

#### Scenario: Arbitrary example tokens are stored
- **WHEN** the initial firmware loads syntactically valid tokens such as future hardware identifiers without corresponding catalog entries
- **THEN** it preserves and ignores them while all existing target behavior remains unchanged

#### Scenario: Empty default profile is used
- **WHEN** no target-profile record has been provisioned
- **THEN** existing physical and native-sim EF800 Application behavior remains unchanged
