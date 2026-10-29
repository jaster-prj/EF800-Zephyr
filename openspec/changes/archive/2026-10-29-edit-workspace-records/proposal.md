## Why

The Workspace viewer currently allows users to inspect records but not correct or remove an individual row without changing the file through another tool. Adding bounded raw-text row actions makes the stored workspace maintainable on the device while protecting the file from interrupted updates and concurrent MTP changes.

## What Changes

- Add row selection and Modify/Delete/Cancel actions to `Show Measurements`, including position and plugin-owned records.
- Add a bounded inline text editor with insert/overwrite, character deletion, save, and cancel behavior; rows longer than 255 bytes remain deletable but are not editable.
- Support CR, LF, and CRLF row boundaries and preserve the selected row's existing ending.
- Replace edited workspace content through a crash-recoverable temporary-file process and reject commits when the source changed during editing, including while MTP is active.
- Keep workspace-derived point/code context consistent after a row is changed or removed.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `workspace-file-browser`: extend the read-only browser contract with row selection, raw-text modification and deletion, input behavior, and safe persistence requirements.

## Impact

- MainApplication workspace viewer and keyboard/LCD interaction in `application/ef800-main/src/main.c`.
- Workspace and persistence APIs/backends under `shared/ef800_persistence/`, including safe replacement and conflict detection.
- Persistence fake and regression coverage for mixed line endings, row edits/deletes, recovery, and conflicting changes.
- The existing `workspace-file-browser` specification; no new capability is introduced.
