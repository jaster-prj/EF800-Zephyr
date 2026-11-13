## Context

`Show Measurements` currently renders four rows from the selected `<workspace>.dat` file, stores byte offsets for the visible range, and uses Up/Down to scroll the page. It has no selected-row state. Measurement, position, and plugin-produced records are opaque text with mixed CR, LF, and CRLF endings; the current viewer ignores CR and splits on LF, so it does not currently model CR-only rows correctly.

The persistence abstraction supports whole-file read/write, bounded range reads, append, stat, list, and remove. It has no rename, sync, streaming-writer, transaction, or replacement-recovery operation. Main MTP exposes the Data tree containing workspaces while EF800Application remains active, so local row changes must detect an external source change without disabling MTP. The repository's accepted ADRs include filesystem-mediated concurrent MTP access (ADR-0011), recoverable file-backed updates (ADR-0012), and QSPI partition ownership (ADR-0013); all remain in force. The workspace transaction applies those storage and recovery boundaries to a larger shared-data file without changing the existing workspace format.

The target has bounded memory. The viewer already reads files larger than 4096 bytes in ranges, and the editor must remain bounded independently of total workspace size.

## Goals / Non-Goals

**Goals:**

- Add row selection, Modify/Delete/Cancel actions, delete confirmation, and bounded inline raw-text editing to `Show Measurements`.
- Preserve the byte content and ending of every unselected row and the selected row's original CR, LF, or CRLF ending.
- Support every row type uniformly, including position and plugin-owned text, without parsing or reformatting it during editing.
- Keep editor content bounded to 255 bytes while allowing longer rows to be deleted.
- Apply changes to large workspaces with bounded buffers and a crash-recoverable same-volume replacement protocol.
- Detect a workspace changed by MTP between row loading and local commit, and refresh measurement context after a successful change.

**Non-Goals:**

- Changing the format or semantics of measurement, position, or plugin-owned records.
- Editing `Show current Position`; it remains read-only.
- Adding record validation, sorting, filtering, multi-row actions, undo history, or a general file manager.
- Disabling MTP while the viewer is open.
- Claiming a cross-process lock with MTP unless the MTP filesystem operations can be coordinated through a shared mutation gate.

## Decisions

### Track rows by byte ranges and recognize all supported endings

Represent a logical row with absolute start/end offsets for its text and ending. A bounded scanner recognizes CR, LF, and CRLF, including delimiter pairs split across read chunks. CRLF is one ending; a final row without a delimiter remains unterminated. Selection stores the selected row's absolute range and the source version used to display it, rather than a whole-file copy or an unbounded line index. Up/Down moves to adjacent row ranges; the existing bounded display window is shifted as necessary.

The editor holds at most 255 text bytes plus a terminator in fixed storage. The LCD renders a viewport of that text with a visible caret and insert/overwrite indicator. Left/Right moves within the row; Insert toggles mode; Backspace/Delete alter the buffer; Enter requests save; Escape discards the buffer. Row-ending bytes are not part of editable text, and CR/LF input is rejected so one edit cannot create or merge rows. When a row exceeds the limit, Modify is unavailable with a diagnostic but Delete remains available.

**Alternative considered:** Load or index the whole workspace in RAM. Rejected because workspace files may exceed the current 4096-byte viewer buffer and target memory is bounded.

### Build replacements by streaming unchanged ranges

Use the persistence/workspace layer for the transformation, not UI-owned filesystem operations. Retain the existing truncate-write and append operations for bounded staging, and add persistence `sync` and `rename` operations. `sync` opens a staged file, calls Zephyr `fs_sync`, and closes it; the selected FAT driver implements this with FatFs `f_sync`. `rename` accepts source and destination names on the same persistence root and requires the destination to be absent. The Zephyr FAT adapter must check this precondition because its current `fs_rename` implementation unlinks an existing destination before calling FatFs. Filesystem operations remain backend-owned so fake persistence can inject failures and test the protocol.

The transformation streams the prefix before the selected row, the edited text when saving, and the suffix after the selected row into a sibling temporary file using a fixed-size buffer. Deletion omits the selected row and its ending. No bytes outside the selected row range are normalized.

**Alternative considered:** Read the full workspace into a snapshot and rewrite it. Rejected because there is no file-size bound and the viewer's 4096-byte snapshot limit is intentional.

### Journal same-volume promotion and recover before workspace use

Do not treat a single FAT rename as sufficient crash protection. Write and synchronize the complete staged replacement first, then write a versioned transaction record before renaming the original to a backup and promoting the staged file. Keep two alternating, checksummed journal slots so a torn update leaves the previous valid phase record available. Journal phases are PREPARED, BACKED_UP, and COMMITTED. Recovery runs before workspace access: before COMMITTED it restores the backup when present (or retains the untouched original); after COMMITTED it retains the verified replacement and cleans up backup/journal artifacts. A pre-journal interruption leaves the original intact and permits cleanup of an orphaned stage. The protocol is designed to recover a complete old or new file after an interruption when the FAT volume remains mountable; a torn/invalid journal with ambiguous file state fails closed rather than guessing which workspace to discard.

Staging, backup, and transaction artifacts use reserved names on the same Data filesystem as the selected workspace so rename does not cross volumes. They must not be interpreted as workspaces, and cleanup must be limited to artifacts owned by this transaction. Since Main MTP exposes the Data tree, implementation must account for possible host visibility or interference with reserved artifacts; dot-prefixed names alone are not assumed to be hidden by MTP. Zephyr's generic `fs_sync` synchronizes file contents, not directory rename metadata; the API exposes no directory-sync operation. Therefore software recovery tests do not alone prove power-cut durability of FAT directory updates, and physical power-loss qualification remains required.

**Alternative considered:** Rewrite `workspace.dat` in place or truncate and rewrite it. Rejected because interruption could destroy the only copy. A bare temporary-file rename is also insufficient because replacement may require multiple FAT metadata operations and needs a recovery decision after reset.

### Use optimistic source-version validation with MTP active

Capture a version token for the selected source when its row is loaded, including file size and a 64-bit FNV-1a content fingerprint computed with bounded streaming reads. Immediately before beginning promotion, recompute and compare the token. If they differ, discard the staged edit, leave the current workspace untouched, and ask the user to reload. Perform this check for both Delete and Modify and regardless of USB personality.

This detects MTP changes during the editing interval without disabling host access. The pinned ZephyrMTP module performs file mutations directly through Zephyr filesystem APIs and exposes no shared application mutation gate; its USB command processing does not serialize those operations against EF800Application filesystem calls. No new gate is added in this change. A host write can therefore race after validation and before/during promotion. Document and test this limitation rather than claiming stronger isolation. The fingerprint is for accidental-change detection, not authentication.

**Alternative considered:** Disable local changes whenever MTP is active. Rejected because the confirmed requirement is to keep local editing available and reject detected conflicts. A full-file RAM snapshot is rejected for bounded-memory reasons.

### Recompute workspace measurement context from committed contents

After promotion succeeds, rescan the resulting file through bounded reads and rebuild the cached point number/code using the existing workspace record interpretation rules. Do not derive context only from the edited text: deletion of the latest valid record must fall back to the preceding valid record, and position or malformed rows must not become measurement context.

### Keep the current-position screen read-only

Only `Show Measurements` gains selection and actions. `Show current Position` remains a read-only summary, avoiding two different editors for the same mixed workspace file and preserving its focused display behavior.

## Risks / Trade-offs

- **[Risk] FAT directory metadata may not be durable when a rename is interrupted; Zephyr exposes file sync but no directory sync.** -> Use alternating, synchronized, checksummed journal slots and test every recoverable phase; fail closed on ambiguous state and retain physical power-loss qualification as a release gate.
- **[Risk] MTP may expose or modify staging, backup, or journal files because those artifacts share the exported Data tree.** -> Reserve unambiguous transaction names, validate staging before promotion, narrowly scope cleanup, and test with an active MTP host. Do not rely on hidden-file behavior.
- **[Risk] Optimistic conflict validation has a time-of-check/time-of-use window because the pinned MTP module has no shared mutation gate.** -> Retain the immediate pre-promotion source check, test writes before that check, and clearly document that a race after validation is not excluded.
- **[Risk] Correct CR/LF/CRLF parsing across chunk boundaries can create off-by-one edits or accidental row merging.** -> Keep text and ending offsets distinct and test all delimiter forms, mixed files, empty rows, and boundaries at range edges.
- **[Risk] The LCD width is smaller than the 255-byte editor limit.** -> Use a horizontal editor viewport that keeps the caret visible and clearly indicates insert/overwrite mode.
- **[Risk] Recomputing context can make edits reveal malformed or legacy records that do not match current parsing rules.** -> Reuse the established parser/skip behavior and test malformed, position, plugin, and empty rows without rewriting them.

## Migration Plan

No workspace migration or record-format change is required. Existing files remain raw byte streams and are edited only at selected row ranges. Before enabling row actions, startup performs transaction recovery for any recognized workspace replacement journal. If recovery cannot safely establish a complete source file, it reports an error and does not permit further edits to that workspace.

Deployment rollback is a firmware rollback; successfully edited workspace files remain valid under the existing raw-text format. Recovery artifacts are cleaned after commit or rollback. The implementation must not delete unrelated files during recovery.

## Open Questions

- The pinned MTP implementation has no shared mutation gate, so the final version check cannot exclude a host write racing after validation. A future MTP/storage change would be needed to remove this limitation.
- Zephyr v4.4.2 exposes `fs_sync` for open files (FatFs `f_sync`) and same-mount `fs_rename`, but no directory-sync API. Rename metadata durability under physical power loss remains a target qualification question; the software protocol guarantees recovery only when FAT remains mountable and journal/rename metadata is readable.
