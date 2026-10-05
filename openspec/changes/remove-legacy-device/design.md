## Context

EF800 currently stores port assignments twice: a compact 40-byte boot metadata record uses one 4-bit builder ID and one 4-bit connection ID per port, while FAT-backed `port_assignment_N` records already carry a full 32-bit plugin ID. IDs `2` and `3` currently represent Elta3 and FTS3XX/Fennel1 in shared validation, a fixed port-device table, and MainApplication dispatch. External plugin discovery happens before assignment initialization in MainApplication, but the current registry also registers those legacy implementations as built-ins.

The bootloader and MainApplication both consume shared metadata. Port 4 has special USB personality behavior, and DebugDevice, USB MSD, and BlueNRG are not measurement plugins and must remain functional. The metadata partition is fixed at 32 KiB, so a larger fixed record fits without a partition change.

## Goals / Non-Goals

**Goals:**

- Make external loaded measurement plugins the only assignable measurement devices.
- Replace compact builder-only boot assignments with version 2 full plugin-ID records.
- Migrate version 1 metadata safely, explicitly clearing legacy IDs `2` and `3`.
- Reconcile persisted assignment files against the ready external measurement registry after discovery.
- Preserve non-measurement boot, console, USB, and recovery assignments.
- Remove the legacy device table from MainApplication runtime dispatch while retaining protocol implementation code only where packaging requires it.

**Non-Goals:**

- Remapping old Elta3 or FTS3XX assignments to new IDs.
- Making external plugins hot-reloadable or available when storage is unreachable.
- Changing the metadata partition address or firmware image layout.
- Redesigning plugin package validation, ABI loading, or USB ownership.
- Preserving old measurement assignments as a compatibility mode.

## Decisions

### Use an explicit version 2 metadata record

Version 2 will use field-wise little-endian encoding and a fixed record size large enough for five `{plugin_id, connection}` pairs. The CRC will cover the encoded versioned state fields, excluding the stored CRC field. This is preferred over widening the old bit-packed field because it removes builder-ID assumptions and makes future plugin IDs unambiguous.

### Decode version 1 only as a migration input

The shared decoder/metadata reader will recognize version 1 sufficiently to migrate it into version 2, but all version 1 measurement IDs `2` and `3` will be dropped. Version 1 will never be emitted after a successful write. A corrupt or unsupported record follows the existing safe initialization path.

### Keep system assignments in metadata, external plugin assignments in full-ID records

Version 2 metadata will carry full IDs for all supported port assignments so bootloader and MainApplication can preserve system behavior without legacy builder decoding. The FAT-backed assignment records remain authoritative for detailed plugin assignment state, and reconciliation keeps the two sources consistent where applicable.

### Discover before reconcile

MainApplication will discover external plugins before semantic assignment validation. The registry is the source of truth for whether a measurement plugin is currently loaded and ready. A registry entry that is missing, diagnostic-only, non-measurement, or unavailable cannot satisfy a persisted measurement assignment.

### Clear stale assignments instead of preserving diagnostics as active devices

Unknown or unavailable measurement assignments are cleared to `NONE` in memory and, when storage is writable, in both the assignment record and metadata. This avoids executing stale code and prevents the UI from presenting a device that cannot run. Diagnostics may log the removed ID and port but do not become selectable entries.

### Enumerate assignment choices from the external registry

The fixed Elta3/FTS3XX device descriptors are removed from measurement assignment. Port UI enumeration will combine supported fixed system entries with ready external measurement registry entries filtered by port hardware capabilities. This avoids a second device catalog and makes assignment behavior match runtime loading.

### Remove built-in measurement registration

The current built-in Elta3/Fennel1 registry registration is incompatible with Option C and will be removed. External packages are the only measurement registry entries. Legacy protocol functions may remain in the SDK/package source tree but must not be reachable through MainApplication's assignment or runtime dispatch path.

## Risks / Trade-offs

- [Risk] Existing Elta3/Fennel1 users lose persisted assignments. -> [Mitigation] Make the clearing explicit, versioned, logged, and covered by migration tests; do not silently remap IDs.
- [Risk] Bootloader and MainApplication images may be upgraded independently. -> [Mitigation] Keep v1 decoding/migration in shared code and make v2 validation strict; deploy shared metadata support in all L496 images together.
- [Risk] A missing external package causes measurement assignments to disappear. -> [Mitigation] Treat this as intentional, show only ready loaded plugins, and preserve unrelated system assignments.
- [Risk] Port 4 USB personality could regress during metadata rewrite. -> [Mitigation] Preserve existing NVS-authoritative Port 4 handling and add reboot/USB transition regression coverage.
- [Risk] Full-ID records could be interpreted as legacy builder IDs by old code. -> [Mitigation] Bump the explicit metadata/assignment versions and remove legacy semantic resolution from the new path.

## Migration Plan

1. Add version 2 metadata structures, encoding, CRC, validation, and v1 migration helpers in shared code.
2. Update bootloader metadata handling and tests so v1 can migrate and v2 controls boot state safely.
3. Remove built-in measurement registration and legacy device assignment descriptors.
4. Discover external plugins before loading and reconciling assignments in MainApplication.
5. Reconcile metadata and FAT assignment records, clearing IDs `2`, `3`, unknown IDs, and unavailable/non-measurement plugins.
6. Replace port UI enumeration and measurement startup/dispatch with registry-driven external plugin behavior.
7. Run metadata, persistence, plugin, migration, bootloader, native, image-layout, and all firmware build gates.

Rollback requires restoring the previous firmware set and metadata reader together. The new firmware must not be expected to recreate removed legacy assignments.

## Open Questions

- Should version 2 retain `console_target` as a compact port/role field, or should it also become a typed full assignment reference? The current design retains it as a console-specific field while validating it against the version 2 per-port records.
- Should invalid external assignment files be deleted immediately or rewritten as explicit `NONE` records? The design assumes the existing remove-assignment persistence operation unless implementation constraints require a tombstone.
- Are all external measurement plugin IDs guaranteed never to collide with system IDs `1`, `4`, and `5`? The registry/assignment validator must enforce reserved-ID rejection if the package contract does not already guarantee this.
