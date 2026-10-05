## 1. Define Versioned Metadata Contract

- [x] 1.1 Define the version 2 boot metadata wire layout, fixed size, per-port full plugin-ID/connection records, little-endian encoding, CRC coverage, and reserved-field rules.
- [x] 1.2 Add strict version 2 encode/decode/validation helpers and preserve version 1 decoding only as an explicit migration input.
- [x] 1.3 Define migration mapping for version 1 records: preserve valid DebugDevice/MSD/BlueNRG assignments, clear IDs `2` and `3`, reject unsupported legacy assignments, and mark migrated metadata dirty.
- [x] 1.4 Update shared metadata consumers in Bootloader and MainApplication so boot target, recovery, console selection, and metadata writes operate with version 2 records.

## 2. Remove Legacy Measurement Assignment Model

- [x] 2.1 Remove Elta3 and FTS3XX/Fennel1 builder IDs `2` and `3` from persisted assignment validation and fixed port-device descriptors without changing unrelated system IDs.
- [x] 2.2 Remove built-in Elta3/Fennel1 measurement registration from the plugin registry and make external ready measurement packages the only measurement registry entries.
- [x] 2.3 Remove the MainApplication legacy `ef800_device_plugin` serial setup, measurement dispatch, and directional-command branches; retain only the external plugin host path.
- [x] 2.4 Update assignment record encode/decode semantics so full plugin IDs are not inferred as legacy builder IDs and structural persistence validation is separated from registry availability validation.

## 3. Reconcile Assignments After Discovery

- [x] 3.1 Add a startup reconciliation pass after external plugin discovery and assignment loading that validates each measurement assignment against a ready loaded measurement registry entry and compatible port connection.
- [x] 3.2 Clear unknown, missing, non-measurement, invalid, incompatible, and legacy IDs to `NONE` in memory and persist cleanup when storage is writable; keep stale assignments inactive when storage is unavailable.
- [x] 3.3 Preserve and test DebugDevice, USB MSD, BlueNRG, Port 4 USB personality, console selection, and bootloader recovery behavior during reconciliation and metadata rewrite.
- [x] 3.4 Make port-assignment UI enumerate ready compatible external measurement plugins dynamically and never display IDs `2` or `3`.

## 4. Tests, Documentation, And Verification

- [x] 4.1 Add shared metadata tests for version 1 migration, version 2 round trips, full plugin IDs, CRC/size/version rejection, and legacy ID removal.
- [x] 4.2 Add assignment persistence/reconciliation tests for loaded, missing, non-measurement, incompatible, and storage-unavailable plugin states.
- [x] 4.3 Add bootloader and MainApplication regression coverage for metadata writes, console selection, Port 4 USB transitions, and recovery decisions.
- [x] 4.4 Update plugin, migration, metadata, and assignment documentation to describe external-only measurement plugins and the breaking removal of IDs `2` and `3`.
- [x] 4.5 Run native tests, all four firmware builds, image-layout verification, and targeted migration/assignment tests; record that old assignments are cleared rather than remapped.
