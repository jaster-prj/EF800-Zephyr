## Why

The current Workspace menu is exposed directly from the main menu and focuses on workspace management operations. Users need a direct way to inspect the selected workspace's recorded data on the EF800 display, including older entries, without requiring a host-side file reader. The viewer must work for workspace files larger than the current 4096-byte snapshot limit.

## What Changes

- Move workspace-management operations under Settings while retaining their existing behavior.
- Keep a top-level main-menu entry named `Workspace`.
- Add `Show Measurements` to the Workspace menu; it displays every line from the selected `workspace.dat`, including position lines.
- Add `Show current Position` to the Workspace menu; it displays the latest position record found in the selected `workspace.dat`.
- Display workspace lines in file order, with the oldest visible line at the top and newest visible line at the bottom.
- Open the measurement viewer at the newest display window and scroll one line at a time toward older or newer entries using file-position ranges.
- Truncate lines that exceed the display width instead of wrapping them.
- Keep the workspace read buffer at a maximum of 4096 bytes and use `(start, end)` file-position ranges to read the previous or next visible line/window, so files larger than 4096 bytes are supported without increasing RAM usage.

## Capabilities

### New Capabilities

- `workspace-file-browser`: Browse the selected workspace file on-device, including chronological line viewing and latest-position display.

### Modified Capabilities

- `virtual-ef800-platform`: Change the main/settings menu hierarchy and add workspace-file display behavior to the virtual and production UI contract.

## Impact

- `application/ef800-main/src/main.c` for menu hierarchy, navigation, and display behavior.
- `shared/ef800_persistence/ef800_workspace.*` and/or the generic persistence backend API for bounded file access.
- Existing UI and persistence tests, plus native-sim or virtual EF800 acceptance coverage for paging, line truncation, large files, and latest-position extraction.
- Existing workspace data remains compatible; no separate position file is introduced.
