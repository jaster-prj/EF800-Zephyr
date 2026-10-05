## Why

EF800 still treats Elta3 and FTS3XX/Fennel1 as legacy builder devices even though measurement-plugin integration now exists. This duplicates plugin resolution, keeps obsolete IDs `2` and `3` in persisted assignment validation, and allows stale assignments to survive without checking whether the corresponding external plugin is currently loaded. The assignment and metadata contract should use external measurement-plugin IDs only and migrate old records deterministically.

## What Changes

- Remove legacy measurement builder IDs `2` and `3` from boot metadata and port-assignment semantics.
- Make external, currently loaded measurement plugins the only assignable measurement devices.
- Remove Elta3 and Fennel1/FTS3XX from the fixed port-device assignment list and enumerate compatible measurement plugins from the loaded registry instead.
- Migrate boot metadata from the compact v1 builder-ID assignment field to a versioned format that stores full 32-bit plugin IDs and connection IDs per port.
- Decode existing v1 metadata for migration, discard stale legacy measurement assignments, and write the new metadata version.
- Reconcile persisted port assignments against ready loaded measurement plugins after discovery; clear unknown, missing, invalid, or non-measurement plugin assignments to `NONE` and persist the cleanup when storage is writable.
- Preserve non-measurement system assignments such as DebugDevice, USB MSD, and BlueNRG, including bootloader recovery and console-selection behavior.
- Remove the legacy `ef800_device_plugin` dispatch path from MainApplication measurement setup and execution; retain protocol code only where needed by external plugin packages.
- Add migration, assignment reconciliation, metadata-version, bootloader, UI, and regression test coverage.

## Capabilities

### New Capabilities

- `external-measurement-assignment`: Full-ID metadata persistence, loaded-plugin reconciliation, stale-assignment cleanup, and dynamic measurement-plugin assignment.

### Modified Capabilities

- None. The repository has no main `openspec/specs/` capability specifications; existing plugin artifacts are change-local.

## Impact

- Shared metadata encoding, validation, CRC, and migration in `shared/ef800_update/`.
- Bootloader metadata consumption in `application/ef800-bootloader/`.
- MainApplication startup ordering, registry discovery, assignment reconciliation, UART setup, menu rendering, and measurement dispatch.
- Port assignment persistence and UI in `shared/ef800_ports/` and `shared/ef800_update/`.
- Plugin registry and external measurement-plugin contract; legacy builder/device table removal.
- Native regression tests, plugin tests, documentation, and all L496 firmware builds.
- This is a breaking persistence-contract migration for old Elta3/Fennel1 assignments: IDs `2` and `3` are intentionally cleared rather than remapped.
