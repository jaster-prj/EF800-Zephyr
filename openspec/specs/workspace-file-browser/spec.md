# workspace-file-browser Specification

## Purpose

Provide bounded browsing of selected workspace records and the latest stored position without changing workspace contents.

## Requirements

### Requirement: Expose workspace browsing from the main menu
The MainApplication SHALL keep a top-level menu entry named `Workspace` and SHALL provide `Show Measurements` and `Show current Position` entries beneath it. Workspace-management actions such as restoring, selecting, creating, and listing workspaces SHALL remain available under Settings and SHALL continue to operate on the selected workspace.

#### Scenario: User opens the Workspace menu
- **WHEN** the user selects `Workspace` from the main menu
- **THEN** the display presents `Show Measurements` and `Show current Position`

#### Scenario: User opens workspace management
- **WHEN** the user selects the workspace-management entry under Settings
- **THEN** the display presents the existing restore, load, create, and list workspace actions without changing their persistence behavior

### Requirement: Display the selected workspace file chronologically
`Show Measurements` SHALL read the currently selected `workspace.dat` and SHALL display every line in that file, including position records. The visible page SHALL preserve file order, with the oldest visible line at the top and the newest visible line at the bottom. Lines longer than the display width SHALL be truncated rather than wrapped.

#### Scenario: Selected workspace contains mixed records
- **WHEN** the user opens `Show Measurements` for a workspace containing measurement and position lines
- **THEN** the display shows both record types in their original file order

#### Scenario: Newest display window is opened
- **WHEN** the selected workspace contains more lines than fit on one display page
- **THEN** the viewer opens on the newest display window and places the newest visible line at the bottom

#### Scenario: A workspace line exceeds the display width
- **WHEN** a workspace line is longer than one display row
- **THEN** the viewer displays its leading content up to the display width and does not wrap it to another row

### Requirement: Navigate large workspace files with bounded memory
The workspace viewer SHALL support selected workspace files larger than 4096 bytes while retaining a workspace read buffer no larger than 4096 bytes. The viewer SHALL represent the currently displayed file range with start and end file-position pointers, SHALL adjust those pointers to read the previous or next visible line/window when the user navigates, and SHALL stop at file boundaries without wrapping around unexpectedly.

#### Scenario: User moves to older entries
- **WHEN** the user moves up and an older line exists before the current display range
- **THEN** the viewer adjusts the start/end file positions to include the preceding line and preserves oldest-at-top and newest-at-bottom ordering

#### Scenario: User moves to newer entries
- **WHEN** the user moves down and a newer line exists after the current display range
- **THEN** the viewer adjusts the start/end file positions to include the next line toward the end of the file

#### Scenario: File exceeds the legacy snapshot limit
- **WHEN** `workspace.dat` is larger than 4096 bytes
- **THEN** the viewer remains usable with a read buffer no larger than 4096 bytes and does not fail solely because of file size

#### Scenario: Viewer reaches a file boundary
- **WHEN** the user attempts to move older than the first line or newer than the last line
- **THEN** the viewer keeps the corresponding start/end file positions at the boundary and reports no additional line without wrapping

#### Scenario: Current display range is read by file position
- **WHEN** the viewer requests the visible workspace range
- **THEN** it reads only the bounded range identified by the current start/end positions and does not create a full-file snapshot

### Requirement: Show the latest position from the selected workspace
`Show current Position` SHALL scan the selected `workspace.dat`, identify position records using the existing workspace record format, and display the last position record in the file. It SHALL not create or require a separate position file.

#### Scenario: Workspace contains position records
- **WHEN** the user selects `Show current Position` and the file contains one or more position records
- **THEN** the display shows the last position record found in file order

#### Scenario: Workspace contains no position record
- **WHEN** the user selects `Show current Position` and no position record exists
- **THEN** the display shows an explicit no-position state and does not report a stale position

#### Scenario: Large workspace contains a late position record
- **WHEN** a position record occurs after the 4096-byte point in `workspace.dat`
- **THEN** the viewer finds and displays that late position record using bounded reads without increasing the 4096-byte buffer

### Requirement: Handle workspace viewer availability safely
The workspace views SHALL require a selected workspace and SHALL present a diagnosable unavailable or read-failure state when the selected workspace cannot be read. Leaving either view SHALL return to the Workspace menu without changing workspace contents or selection.

#### Scenario: No workspace is selected
- **WHEN** the user opens either workspace view without a selected workspace
- **THEN** the display reports that no workspace is selected and does not attempt to read an arbitrary file

#### Scenario: Workspace read fails
- **WHEN** incremental reading of the selected workspace returns an error
- **THEN** the display reports the read failure and preserves the existing workspace data

#### Scenario: User leaves a workspace view
- **WHEN** the user presses the host back key while viewing measurements or the current position
- **THEN** MainApplication returns to the Workspace menu and releases any viewer state without changing the file
