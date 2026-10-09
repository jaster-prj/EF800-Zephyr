## ADDED Requirements

### Requirement: Exercise workspace browsing in the virtual EF800
The virtual EF800 SHALL expose the production workspace menu hierarchy and SHALL support deterministic automated scenarios for mixed workspace-line viewing, chronological paging, line truncation, large-file access, latest-position extraction, and no-workspace or read-failure handling.

#### Scenario: Virtual test opens the workspace viewer
- **WHEN** an automated virtual EF800 test selects `Workspace` and then `Show Measurements`
- **THEN** the observed production display contains workspace lines in file order with the newest visible line at the bottom

#### Scenario: Virtual test scrolls a large workspace by file position
- **WHEN** a virtual test provides a workspace file larger than 4096 bytes and moves up or down through the displayed lines
- **THEN** the production viewer reads the previous or next range using bounded memory, without a file-size failure or ordering inversion

#### Scenario: Virtual test selects current position
- **WHEN** a virtual test selects `Show current Position` for a mixed workspace file
- **THEN** the production display contains the last position record and ignores earlier position records as the current result

#### Scenario: Virtual test verifies truncation and unavailable state
- **WHEN** a virtual test supplies an overlong line or opens the viewer without a selected workspace
- **THEN** the display truncates the line without wrapping or reports the unavailable state respectively
