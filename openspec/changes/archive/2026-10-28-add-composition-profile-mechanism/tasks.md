## 1. Profile Encoding and Catalog Resolution

- [x] 1.1 Add a shared target-composition module with fixed bounds, profile/snapshot types, strict COMP-Nr. token validation, canonical text encoding, checksum generation, and whole-record decoding for the documented single `composition=` line.
- [x] 1.2 Implement immutable catalog validation and resolution of recognized selections, one default per omitted group, unknown-token classification, and conflict rejection without assigning meanings to any hardware-specific COMP-Nr.
- [x] 1.3 Define the initial empty catalog and verify that missing, empty, invalid, or unavailable profiles resolve to an empty effective composition without changing existing behavior.

## 2. Restart-Scoped Composition Service

- [x] 2.1 Implement bounded active and pending snapshots with inspect, exact-token add/remove, complete replacement, validation, discard, and restart-required state while preserving unknown tokens.
- [x] 2.2 Implement two-slot profile loading with checksum validation, newest-generation selection, wraparound/conflict handling, and conservative fallback when neither slot is valid.
- [x] 2.3 Implement commit to the inactive/older configuration slot with generation increment, read-back decode and equality verification, predecessor retention on failure, and no mutation of the active effective snapshot in the current boot.

## 3. EF800Application and Build Integration

- [x] 3.1 Add the shared module to the common `ef800-application` physical/native-sim source manifest and initialize it after the existing persistence service with the empty catalog.
- [x] 3.2 Add bounded startup diagnostics for active generation, default/empty fallback, unknown tokens, invalid slot conflicts, and storage unavailability without adding a provisioning UI or transport command.
- [x] 3.3 Confirm by code review and targeted tests that composition initialization does not alter LCD, Bluetooth, battery, ports, assignments, menus, plugins, DeviceTree, pin control, or any other current runtime behavior.

## 4. Native Test Coverage

- [x] 4.1 Add a focused native-sim composition test suite covering canonical encode/decode, empty composition, whitespace normalization, CR/LF input, malformed and duplicate fields/tokens, checksum failure, and every published size/count boundary.
- [x] 4.2 Add catalog tests covering empty catalogs, defaults for omitted groups, recognized selections, duplicate catalog entries/defaults, conflicting recognized groups, and unknown-token exclusion from effective state.
- [x] 4.3 Add service tests covering staged add/remove/replace/discard, unknown-token preservation, unchanged active snapshots before restart, generation advancement, slot selection, equal-generation conflict, read-back mismatch, failed writes, torn records, and recovery of the previous valid slot.
- [x] 4.4 Add startup integration coverage proving missing and unavailable profile storage remain non-fatal and arbitrary unknown tokens have no observable hardware or application effect.

## 5. Documentation and Verification

- [x] 5.1 Document the target-profile grammar, COMP-Nr. bounds, two-slot recovery, default and unknown-token semantics, modification API, restart activation, and the rule that concrete catalog entries require later specified changes.
- [x] 5.2 Update `docs/migration-status.md` and test-count documentation for the new suite while retaining the physical QSPI/FAT qualification caveat.
- [x] 5.3 In the reusable devcontainer, run the native test suite, pristine-build EF800Application, BackupApplication, Bootloader, and keyboard firmware, and run `application/scripts/verify-images.sh`; record any hardware-only validation as not exercised rather than weakening it.
