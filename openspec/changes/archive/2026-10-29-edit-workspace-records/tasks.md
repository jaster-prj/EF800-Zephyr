## 1. Bounded workspace row model

- [x] 1.1 Verify Zephyr v4.4.2 FAT file-sync and same-mount rename behavior, confirm the pinned Main MTP module has no shared mutation gate, finalize `sync`/destination-absent `rename` persistence operations, and document the residual MTP race and directory-metadata power-loss limitation.
- [x] 1.2 Implement a bounded row scanner that identifies CR, LF, and CRLF boundaries, including chunk-edge pairs, and returns absolute text/ending offsets for empty and unterminated rows.
- [x] 1.3 Add shared regression tests for mixed endings, empty rows, final unterminated rows, long rows, and delimiters at range-buffer boundaries.

## 2. Recoverable persistence and conflict detection

- [x] 2.1 Extend the persistence abstraction, Zephyr backend, and fake backend with bounded sibling staging, same-volume promotion, synchronization, and narrowly scoped cleanup primitives needed by the transaction.
- [x] 2.2 Implement journaled workspace replacement for row modification and deletion, including startup recovery before workspace access and fail-closed handling of invalid transaction metadata.
- [x] 2.3 Add failure-injection tests for interruption before staging completes, after journal preparation, after original backup, after replacement installation, and during committed cleanup; verify recovery leaves a complete old or new file.
- [x] 2.4 Capture and recheck a bounded source-version fingerprint before promotion; reject conflicts without changing the source and verify the check is applied to both deletion and modification.
- [x] 2.5 Add persistence tests proving untouched bytes/endings are preserved, large files are streamed without file-sized buffers, MTP-visible artifacts are safely validated, and conflict detection catches an intervening host update.
- [x] 2.6 Recompute cached point/code context from the committed workspace using existing record interpretation rules and test deletion/modification of the latest valid measurement record.

## 3. EF800Application row interaction

- [x] 3.1 Add selected-row state to `Show Measurements`; route Up/Down to row selection and bounded scrolling, and Enter to Modify/Delete/Cancel actions while keeping `Show current Position` read-only.
- [x] 3.2 Implement the 255-byte raw-text editor with visible caret and insert/overwrite state, Left/Right, Insert, Backspace/Delete, Enter-save, Escape-cancel, and rejection of row-ending input.
- [x] 3.3 Add explicit delete confirmation; keep over-limit rows deletable, report them as not editable, and preserve the selected row context on cancel/error.
- [x] 3.4 Connect confirmed edits/deletes to transactional persistence, source-conflict reporting, viewer refresh, and safe selection placement after the row changes.
- [x] 3.5 Add native-sim UI tests for row selection/scrolling, row actions, delete confirmation, editor input/rendering, cancel behavior, long-row handling, and read/commit errors.

## 4. Verification and status

- [x] 4.1 Run the keyboard, protocol, RTC, and persistence native tests and confirm the updated native test-case total in `docs/migration-status.md`.
- [x] 4.2 Run the firmware builds prescribed by the repository guide and confirm the EF800Application image contract with `application/scripts/verify-images.sh`.
- [x] 4.3 Update `docs/migration-status.md` and relevant persistence/workspace status documentation with the implemented behavior, test results, MTP concurrency limitation, and any remaining hardware recovery qualification gate.
