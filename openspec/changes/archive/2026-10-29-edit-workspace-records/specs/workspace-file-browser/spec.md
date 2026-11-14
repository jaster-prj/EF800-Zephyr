## ADDED Requirements

### Requirement: Select workspace rows and open row actions
`Show Measurements` SHALL allow the user to select any displayed workspace row, including measurement, position, plugin-owned, and empty rows. Up and Down SHALL move the selected row through the file in chronological order and scroll the bounded display window when needed. Enter SHALL open Modify, Delete, and Cancel actions for the selected row.

#### Scenario: Select a row and open its actions
- **WHEN** the user navigates to a workspace row and presses Enter
- **THEN** the viewer presents Modify, Delete, and Cancel actions for that row

#### Scenario: Selection reaches a row outside the visible window
- **WHEN** the user moves the selection up or down beyond the visible rows
- **THEN** the display scrolls to keep the selected row visible without wrapping at the beginning or end of the file

#### Scenario: Open actions for a position or plugin-owned row
- **WHEN** the selected row is a position record or plugin-owned record
- **THEN** the same Modify, Delete, and Cancel actions are available without interpreting or converting its record format

### Requirement: Delete a selected workspace row
The EF800 Application SHALL delete a selected workspace row only after explicit confirmation. Deletion SHALL remove exactly the selected row and its row ending, when present, without changing the bytes of other rows. A row SHALL remain deletable regardless of its length or record type.

#### Scenario: Confirm deletion
- **WHEN** the user selects Delete and confirms the prompt
- **THEN** the selected row is removed and the viewer refreshes the affected range and selection

#### Scenario: Cancel deletion
- **WHEN** the user cancels the delete confirmation
- **THEN** the workspace file remains unchanged and the viewer returns to row actions or browsing

#### Scenario: Delete a row longer than the edit limit
- **WHEN** the selected row is longer than 255 bytes and the user confirms Delete
- **THEN** the row is removed while the remaining workspace bytes are preserved

### Requirement: Modify a selected workspace row as bounded raw text
The EF800 Application SHALL provide an inline editor for the selected row's raw text, independent of measurement, position, or plugin record semantics. The editor SHALL support Left/Right caret movement, Insert-mode toggling between insertion and overwrite, Backspace and Delete character removal, Enter to save, and Escape to cancel. It SHALL not allow inserted characters to create a new row. The editor SHALL visibly indicate the caret and current insert/overwrite mode, and SHALL limit editable row content to 255 bytes; longer rows SHALL remain deletable but SHALL NOT be editable.

#### Scenario: Insert and overwrite characters
- **WHEN** the user moves the caret, toggles Insert, and types within an editable row
- **THEN** characters are inserted or overwritten at the caret according to the selected mode without changing the row ending

#### Scenario: Remove characters and save
- **WHEN** the user uses Backspace or Delete and presses Enter
- **THEN** the edited row content is saved while the original row ending and all other row bytes are preserved

#### Scenario: Cancel row modification
- **WHEN** the user presses Escape while editing
- **THEN** the editor is discarded and the workspace file remains unchanged

#### Scenario: Attempt to modify a row over 255 bytes
- **WHEN** the user selects Modify for a row over 255 bytes
- **THEN** the application reports that the row is too long to edit and leaves the row unchanged

#### Scenario: Edit an unterminated final row
- **WHEN** the selected final row has no row ending and the user saves a modification
- **THEN** the row remains unterminated and no row ending is added

### Requirement: Recognize and preserve workspace row endings
Workspace browsing and row editing SHALL recognize CR, LF, and CRLF as logical row endings, treating CRLF as one ending. An edit SHALL preserve the selected row's original ending, and deletion SHALL preserve the bytes and endings of every other row.

#### Scenario: Browse mixed row endings
- **WHEN** a workspace contains rows separated by CR, LF, and CRLF
- **THEN** each logical row is independently selectable and displayed in file order

#### Scenario: Modify rows with each supported ending
- **WHEN** the user saves an edit to a row ending in CR, LF, or CRLF
- **THEN** the saved row retains exactly its original ending

### Requirement: Commit workspace row changes with crash-recoverable replacement
The persistence layer SHALL apply confirmed row deletions and saved modifications by constructing a complete replacement in temporary storage and promoting it through a recoverable replacement procedure. At every interruption point, startup recovery SHALL leave the selected workspace as either the complete pre-edit file or the complete replacement, never a partial or truncated mixture. Replacement SHALL retain bounded-memory operation for workspace files larger than the viewer's 4096-byte range buffer.

#### Scenario: Power is lost during replacement preparation
- **WHEN** power is lost while the replacement is being built and the original workspace is still intact
- **THEN** startup recovery retains the complete original workspace and discards or resumes incomplete temporary state safely

#### Scenario: Power is lost during promotion
- **WHEN** power is lost while the replacement transaction is promoting the new workspace
- **THEN** startup recovery identifies the transaction state and restores either the complete original or the complete replacement before workspace use

#### Scenario: Replacement succeeds for a large workspace
- **WHEN** a row in a workspace larger than 4096 bytes is modified or deleted
- **THEN** replacement streams unchanged ranges without allocating memory proportional to file size

### Requirement: Reject workspace edits when the source changed during editing
Before committing a row modification or deletion, the EF800 Application SHALL verify that the selected workspace still matches the source version from which the row was selected. If the source changed, the application SHALL reject the edit, preserve the current workspace file, and report a conflict so the user can reload the viewer. This check SHALL also be performed while MTP is active.

#### Scenario: MTP changes the workspace during editing
- **WHEN** MTP changes the selected workspace after the row is loaded and before local commit
- **THEN** the local modification or deletion is rejected and the MTP-updated workspace remains intact

#### Scenario: Source remains unchanged
- **WHEN** the source workspace matches the version captured for the edit at commit validation
- **THEN** the local row change proceeds through the recoverable replacement procedure

### Requirement: Refresh workspace-derived context after row changes
After a row modification or deletion succeeds, the workspace's cached measurement point number and code SHALL be recalculated from the resulting file using the existing record interpretation rules before a later measurement is appended.

#### Scenario: Delete or modify the latest valid measurement record
- **WHEN** a row change removes or changes the most recent valid measurement record
- **THEN** a subsequent measurement append uses the latest valid point number and code remaining in the workspace, or the existing empty-context defaults when none remain

## MODIFIED Requirements

### Requirement: Display the selected workspace file chronologically
`Show Measurements` SHALL read the currently selected `workspace.dat` and SHALL display every logical row in that file, including position records and empty rows. The visible page SHALL preserve file order, with the oldest visible row at the top and the newest visible row at the bottom. Lines longer than the display width SHALL be truncated rather than wrapped. The selected row SHALL be visibly distinguishable from other displayed rows.

#### Scenario: Selected workspace contains mixed records
- **WHEN** the user opens `Show Measurements` for a workspace containing measurement, position, plugin-owned, or empty rows
- **THEN** the display shows those rows in original file order and makes the selected row apparent

#### Scenario: Newest display window is opened
- **WHEN** the selected workspace contains more rows than fit on one display page
- **THEN** the viewer opens on the newest display window and places the newest visible row at the bottom

#### Scenario: A workspace line exceeds the display width
- **WHEN** a workspace line is longer than one display row
- **THEN** the viewer displays its leading content up to the display width and does not wrap it to another row

### Requirement: Handle workspace viewer availability safely
The workspace views SHALL require a selected workspace and SHALL present a diagnosable unavailable or read-failure state when the selected workspace cannot be read. Leaving either view SHALL return to the Workspace menu. Canceling row actions, deletion confirmation, or row editing SHALL leave the workspace contents unchanged; successful confirmed row changes SHALL follow the row-change requirements. `Show current Position` SHALL remain read-only.

#### Scenario: No workspace is selected
- **WHEN** the user opens either workspace view without a selected workspace
- **THEN** the display reports that no workspace is selected and does not attempt to read an arbitrary file

#### Scenario: Workspace read fails
- **WHEN** incremental reading of the selected workspace returns an error
- **THEN** the display reports the read failure and preserves the existing workspace data

#### Scenario: User cancels a row edit or action
- **WHEN** the user cancels row actions, deletion confirmation, or row editing
- **THEN** EF800 Application returns to the appropriate browsing state without modifying the workspace file

#### Scenario: User leaves a workspace view
- **WHEN** the user presses the host back key while viewing measurements or the current position
- **THEN** EF800 Application returns to the Workspace menu and releases viewer state without applying an unconfirmed change

#### Scenario: User views current position
- **WHEN** the user opens `Show current Position`
- **THEN** the last stored position is displayed without exposing row modification or deletion actions
