# keyboard-diagnostics Specification

## Purpose

Provides EF800Application diagnostics for checking the live keyboard application version and mode through its app-level I²C interface.

## Requirements

### Requirement: EF800Application reports live keyboard version and mode
EF800Application SHALL provide a `keyboard version` shell command and a status query that read the current version and execution mode from the keyboard application over I²C. They SHALL NOT present cached data as the result of a failed or incomplete query.

#### Scenario: Keyboard application responds with version and mode
- **WHEN** the keyboard application is running and the user queries version or status
- **THEN** EF800Application reports a fresh complete version and identifies application mode

#### Scenario: Application does not respond
- **WHEN** the app-level I²C query does not receive a valid response
- **THEN** EF800Application reports that application status/version is unavailable and does not print stale data as current

#### Scenario: Query overlaps another keyboard command
- **WHEN** another keyboard command is active while version/status is requested
- **THEN** EF800Application serializes I²C transactions and accepts only the response matching that query
