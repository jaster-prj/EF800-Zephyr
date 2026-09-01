# EF800 Persistence Plan

## Goal

Port the Mbed OS persistence and Workspace behavior to Zephyr without making
the new firmware dependent on native C/C++ struct layout or on an unverified
QSPI device. The persistence layer must be testable on `native_sim` and must
never format a failed filesystem mount.

## Compatibility Contract

The legacy logical layout is retained:

```text
configuration: .workspace, time settings, port assignments, device settings
data:          <workspace-name>.dat
```

On Zephyr these roots are mounted below `/EF800:`:

```text
/EF800:/.configuration/
/EF800:/data/
```

Workspace files use the `.dat` suffix. The `.workspace` file contains the
selected filename, with optional trailing CR/LF accepted for compatibility with
the legacy implementation.

New persistence records must use an explicitly defined, versioned format. The
Mbed `Persistence<T>` files contain native `int`, `bool`, enum, pointer, and
padding representations and are not portable between toolchains. Zephyr must
not write new records in that format.

Existing Mbed records are read-only migration inputs. A compatibility reader is
required only if deployed Mbed settings must survive the firmware migration.
The migration reader must validate file length, record count, IDs, and bounds
before accepting any record.

## Phase 1: Backend and Workspace Foundation

Status: implemented in this increment.

- Define a backend-neutral persistence API for read, write, append, stat, list,
  and remove operations.
- Centralize path/name validation and reject absolute paths, empty components,
  `.`/`..`, and unsupported characters.
- Provide a Zephyr filesystem adapter that returns `-ENOTSUP` while the
  MainApplication storage service is disabled.
- Provide an in-memory fake backend for native tests.
- Implement Workspace create, select, restore-current, current-file query, and
  append operations on top of the backend.
- Keep creation non-destructive: creating an existing workspace returns
  `-EEXIST` rather than truncating user data.
- Accept a trailing CR/LF in `.workspace`, but write the selected filename
  without a newline.

## Phase 2: MainApplication Integration

Status: implemented in this increment.

- Add Workspace entries below the existing Workspace menu.
- Add keyboard dialogs for restore, existing-workspace selection, and new
  workspace creation.
- Display storage-unavailable and file-operation errors without blocking the
  main loop.
- Keep list and selection navigation bounded and return to the correct parent
  menu on left, Escape, or Backspace.
- Keep `CONFIG_EF800_MAIN_STORAGE=n` for native and hardware bring-up builds
  until the QSPI device is identified.

## Phase 3: Settings and Format Migration

- Keep the current Zephyr text format for timezone settings:
  `<offset_minutes> <dst>\n`.
- Decide whether Mbed binary settings must be migrated from deployed devices.
- If required, add fixtures for legacy time, debug, Elta3, and FTS3XX records.
- Write only the new validated format after a successful migration.
- Add version and checksum fields before persisting device configurations.

## Position Records

Status: implemented in this increment.

- Validate STP and code text using the legacy allowed-character set.
- Parse height as signed metres with at most three fractional digits.
- Store the value internally as signed millimetres to avoid floating-point
  formatting differences between toolchains.
- Append the legacy logical record shape with a normalized three-digit fraction:
  `-<stp> <height-metres> <code> \r`.
- Require an explicitly selected Workspace before appending.
- Keep Position UI input and append errors bounded and visible.

## Phase 4: QSPI/FAT Validation

Before enabling `CONFIG_EF800_MAIN_STORAGE`:

- Identify the fitted NOR by schematic, JEDEC ID, or SFDP.
- Confirm capacity, erase geometry, program size, and maximum frequency.
- Confirm that the existing FAT volume is readable.
- Verify `.configuration` and `data` directory behavior.
- Verify that mount failure does not format the volume.
- Verify that MainApplication FAT access and backup USB MSC access are mutually
  exclusive.
- Exercise power loss around writes, appends, metadata updates, and deletes.

## Phase 5: Port and Device Persistence

Status: typed port assignments, five-port metadata, connection-aware routing,
cross-image console selection, Elta3/FTS3XX protocol and record handling,
assigned-port UART setup, and the MainApplication measurement workflow are
implemented and verified. Hardware pin ownership and device-specific
configuration persistence remain unfinished; see `docs/migration-checkpoint.md`.

- Define typed port-assignment records with explicit version and checksum.
- Validate builder-ID lookup, port IDs, and capability compatibility.
- Add Debug, FTS3XX, and Elta3 configuration persistence only after typed port
  ownership and UART routing are resolved.
- Keep USB MSD persistence last because it shares the QSPI ownership boundary.

## Verification Gates

Every phase must pass:

- native persistence and Workspace tests
- the existing keyboard/protocol/RTC tests
- all four EF800 firmware builds
- `scripts/verify-images.sh`

Persistence tests must cover missing files, malformed `.workspace`, CR/LF
compatibility, invalid names, path traversal, duplicate creation, append
behavior, deletion, list filtering, and storage-unavailable behavior.
