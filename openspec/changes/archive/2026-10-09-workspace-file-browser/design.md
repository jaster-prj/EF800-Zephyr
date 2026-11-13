## Context

The current EF800Application exposes the complete Workspace management menu from the main menu. The workspace abstraction selects one `<name>.dat` file and appends both measurement and position records to that file. `ef800_workspace_snapshot()` reads the entire selected file into a caller-provided buffer capped by `EF800_WORKSPACE_SNAPSHOT_MAX` (4096 bytes), which is unsuitable for a navigable viewer as files grow. The target devices have limited RAM, so this 4096-byte working-buffer limit must remain.

The LCD presents a narrow text body, so the viewer has only a bounded number of rows and a fixed display width. Existing UI navigation already distinguishes main, child, dialog, and editor menus, and the native-sim target exercises the production EF800Application path. The design must preserve existing workspace data and avoid introducing a separate position file.

## Goals / Non-Goals

**Goals:**

- Make the main-menu `Workspace` entry a read-oriented entry point with `Show Measurements` and `Show current Position`.
- Move existing workspace lifecycle operations under Settings without changing their persistence semantics.
- Read the selected `workspace.dat` through bounded file-position ranges so files larger than 4096 bytes are supported without increasing the 4096-byte working buffer.
- Present all workspace lines in file order, with the newest visible line at the bottom of the display window.
- Locate and display the last position record without changing the on-disk format.
- Truncate overlong display lines and cover behavior through shared, native-sim, and virtual tests.

**Non-Goals:**

- Splitting measurements and positions into separate files.
- Changing measurement or position record formats.
- Editing, deleting, sorting, filtering, or appending records from the viewer.
- Adding a general browser for files outside the selected workspace.
- Changing plugin-owned measurement behavior or measurement persistence.

## Decisions

### Keep one selected workspace file

The viewer continues to use `ef800_workspace_get_filename()` and the selected workspace abstraction. Position records remain mixed with measurement records in the existing file. This avoids migration and preserves compatibility with files already written by the application.

### Move management, not ownership

The existing restore/load/create/list actions move into a Workspace-management child menu under Settings. The main-menu Workspace entry owns only read-only views. Back navigation returns from the views to the Workspace menu and from management to Settings, maintaining the established menu hierarchy.

### Use a fixed 4096-byte range reader

The persistence/workspace layer shall expose a bounded range operation for the selected file. The operation uses a maximum 4096-byte working buffer and file-position pointers represented as `(start, end)` offsets. It may use backend-specific seek/read operations internally, but the UI must not depend on `ef800_workspace_snapshot()` or allocate a buffer proportional to the file size.

The range reader must align reads to logical line boundaries. If a read begins or ends in the middle of a line, it adjusts the corresponding pointer to the preceding or following newline before returning displayable lines. A line longer than the display width is retained only as needed for parsing and is truncated by the renderer; a line longer than the working buffer can be traversed in bounded chunks without increasing the buffer.

### Scroll by adjacent file-position ranges

Opening `Show Measurements` identifies the final display window and stores its start/end file positions. The visible lines remain in ascending file order, so the newest visible line is at the bottom. Moving up calculates a preceding range by seeking backward from the current start and locating the prior line boundary. Moving down calculates a following range by seeking forward from the current end and locating the next line boundary. The range is then rendered with the same bounded buffer. Boundary movement is clamped and does not wrap.

The viewer state retains only the current start/end positions, the bounded display lines, and bounded scan metadata. It does not retain a copy of the whole file or an unbounded line-offset index.

### Find the latest position by forward scan

`Show current Position` scans the selected file from beginning to end using successive bounded ranges and replaces a bounded stored candidate whenever a position record is encountered. At end of file, the candidate is the latest position. No candidate produces an explicit no-position display. The operation uses the same 4096-byte range reader and remains valid when the final position occurs beyond 4096 bytes.

### Keep display truncation at the renderer boundary

The file reader and record recognizer retain enough bounded content to identify records, while the LCD rendering helper truncates each visible line to the display width. Lines are not wrapped, and truncation does not alter stored data.

### Test through shared behavior and native simulation

Unit tests should cover page calculations, boundary behavior, mixed-line ordering, latest-position selection, truncation, and reader errors with fake persistence. Native-sim or virtual EF800 tests should drive the real menu and keyboard path and inspect the production LCD output for large files and menu placement.

## Risks / Trade-offs

- **[Risk] Seeking backward to find line boundaries can be slow on a backend without efficient random access.** -> Keep the range bounded, use backend buffering, and make each move read only the adjacent line/window; measure native and target behavior before considering an index.
- **[Risk] A backend range-read API can affect multiple persistence fakes and adapters.** -> Define the smallest start/end contract possible and add conformance tests for the Zephyr and fake backends.
- **[Risk] Existing lines may be malformed or longer than the reader's logical-line buffer.** -> Treat malformed content as displayable text where possible, preserve truncation behavior, and report only actual I/O or structural reader failures.
- **[Risk] The name `Show Measurements` implies filtering even though it intentionally shows all workspace lines.** -> Make the all-lines behavior explicit in display/help text and specification tests; do not silently filter position records.
- **[Risk] Moving the management menu can break existing shortcut and back-navigation expectations.** -> Preserve existing action shortcuts where possible and add UI regression tests for both main and Settings paths.

## Migration Plan

No file migration is required. Existing selected workspace metadata and mixed `workspace.dat` files remain valid. The implementation should retain the old whole-file snapshot API for existing callers unless it is proven unused, while routing the new viewer through the fixed-size range reader. If the persistence interface changes, update all backends and fakes together and keep the old API as a compatibility wrapper where practical.

Rollback consists of reverting the UI and reader changes; no on-disk data is rewritten by the viewer.

## Open Questions

- Whether the display should show a short file name/header or reserve all body rows for workspace lines; the core ordering, fixed-buffer, and start/end pointer behavior is independent of that presentation choice.
