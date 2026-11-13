## 1. Fixed-Buffer File-Position Reading

- [x] 1.1 Define a workspace range-read contract with a maximum 4096-byte working buffer and `(start, end)` file-position pointers.
- [x] 1.2 Implement forward and backward line-boundary seeking for the Zephyr persistence backend while preserving EOF and read-error behavior.
- [x] 1.3 Implement the same start/end range behavior for the fake/native test persistence backend and add conformance coverage for files larger than 4096 bytes.
- [x] 1.4 Add workspace-level helpers for reading bounded logical lines and identifying the existing position-record format without changing on-disk records.

## 2. Workspace Viewer Behavior

- [x] 2.1 Add bounded viewer state for the current start/end file positions, visible lines, and read diagnostics.
- [x] 2.2 Implement newest-window initialization and one-line older/newer navigation by calculating preceding/following file-position ranges.
- [x] 2.3 Implement `Show Measurements` to display every selected `workspace.dat` line, including position lines, with display-width truncation and no wrapping.
- [x] 2.4 Implement `Show current Position` by scanning the selected workspace and retaining the last position record, including the explicit no-position state.
- [x] 2.5 Handle no selected workspace, missing/read-failing files, empty files, and viewer back-navigation without mutating workspace data.

## 3. EF800Application Menu Integration

- [x] 3.1 Move restore, load, create, and list workspace actions into a workspace-management child menu under Settings while preserving their existing behavior and shortcuts where possible.
- [x] 3.2 Keep the main-menu entry named `Workspace` and add `Show Measurements` and `Show current Position` entries with correct selection counts and display rendering.
- [x] 3.3 Integrate viewer keyboard navigation and back behavior with the existing application menu state machine without interfering with measurement editors or plugin sessions.
- [x] 3.4 Verify the selected workspace header/status behavior remains correct across the new main-menu and Settings paths.

## 4. Tests and Virtual EF800 Coverage

- [x] 4.1 Add shared workspace-reader tests for mixed records, ordering, start/end range calculations, adjacent-line navigation, file boundaries, truncation, large files, latest-position extraction, and read failures.
- [x] 4.2 Add shared UI tests for the revised main/Settings hierarchy and viewer back-navigation.
- [ ] 4.3 Add native-sim or virtual EF800 fixtures containing a workspace larger than 4096 bytes and exercise newest-window, older-line, newer-line, and boundary behavior through the real keyboard/display path while keeping the read buffer at 4096 bytes.
- [ ] 4.4 Add virtual coverage for current-position selection, no-position state, no selected workspace, and overlong-line truncation.
- [x] 4.5 Run the complete relevant native test suite and update migration/status documentation if the project’s documented test baseline changes.
