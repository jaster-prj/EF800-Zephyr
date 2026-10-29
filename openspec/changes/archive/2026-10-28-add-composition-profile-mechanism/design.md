## Context

EF800 already stores configuration files under `/EF800:/.configuration/` through the backend-neutral `ef800_persistence` API. Physical firmware maps that API to QSPI/FAT, native tests use an in-memory fake, and MainApplication can continue with storage unavailable. Existing settings use explicit encodings, while boot-critical USB and fallback data remain separately projected into internal-flash metadata.

There is currently no target composition identity or generic way to resolve persisted component identifiers into defaults. MainApplication also directly constructs its current hardware services. This change establishes only the persistence and resolution mechanism; it must not reinterpret those services yet. The same mechanism must build for physical EF800 and `native_sim`, consistent with ADR-0006, and profile changes use an explicit restart boundary consistent with the existing startup-only activation pattern in ADR-0008. Private configuration remains outside the MTP roots described by ADR-0011.

## Goals / Non-Goals

**Goals:**

- Define a bounded, versioned, integrity-protected target-profile encoding with one space-separated `composition=` line.
- Preserve unknown tokens while resolving known catalog groups and defaults.
- Provide a shared service for immutable active snapshots and staged, verified modifications.
- Recover the newest complete valid record after an interrupted update.
- Keep initial physical and native-sim runtime behavior unchanged.
- Provide extension seams for later catalog entries and composition consumers.

**Non-Goals:**

- Define any real or example COMP-Nr. meaning, including LCD, Bluetooth, battery, storage, or port variants.
- Add factories, switch drivers, filter menus, alter DeviceTree/pinctrl, or reconcile plugin/device assignments.
- Expose private configuration through MTP or add a front-panel, browser, or protocol provisioning UI.
- Move the profile into internal boot metadata or a new QSPI partition.
- Hot-recompose services in the running application.

## Decisions

### Use one logical profile with two recoverable file slots

The logical target profile will be stored as two bounded configuration records, for example `target_profile_a` and `target_profile_b`, under `EF800_PERSISTENCE_CONFIGURATION`. Each record contains `format_version`, `product`, `generation`, one `composition=` line, and a checksum over a canonical encoding. Startup validates both slots and selects the valid record with the newest generation; equal or ambiguous wrapped generations are diagnosed and resolved conservatively.

Commit writes only the inactive/older slot, reads it back through the persistence API, decodes it, and compares it with the pending profile before reporting success. The active in-memory snapshot is not replaced during that boot. This avoids requiring atomic rename support from every persistence backend and retains a valid predecessor when a write is torn.

Alternatives considered:

- One file overwritten in place: simpler, but a power loss can destroy the only target identity.
- Temporary file plus FAT rename: familiar, but requires expanding the persistence API and still couples correctness to filesystem rename guarantees.
- Internal NVS: robust but conflicts with the requirement for file-based provisioning and with the separation of boot metadata from ordinary configuration.

### Keep the text grammar strict and bounded

The canonical record uses ASCII `key=value` lines with exactly one occurrence of each required field and exactly one `composition=` line. Tokens are separated by one or more ASCII spaces on input and emitted with one space in canonical output. A token starts with an uppercase ASCII letter, continues with uppercase letters or digits, and is limited to 16 characters; the profile holds at most 32 distinct tokens and remains bounded to a small fixed buffer. Duplicate tokens and duplicate fields reject the whole candidate.

The checksum is calculated over canonical fields excluding the checksum line, making read-back and test vectors deterministic. CR/LF may be accepted consistently with existing migration readers, but canonical output uses LF.

Alternatives considered:

- JSON or CBOR: more general than needed and adds parser/schema complexity for a small manufacturing profile.
- Repeated `comp=` keys: rejected by the product requirement for one readable space-separated composition line.
- Free-form token punctuation: makes group recognition and future tooling less predictable.

### Separate stored tokens from catalog-resolved state

The shared module will represent:

```text
stored profile
    +-- recognized selections
    +-- syntactically valid unknown tokens
    +-- generation and source slot

effective snapshot
    +-- recognized selections
    +-- defaults for omitted registered groups
```

The catalog is an immutable table supplied to the resolver. A generic entry identifies a token, group, whether it is the group default, and opaque selection metadata reserved for future consumers. The resolver validates at most one recognized selection per group, applies one declared default to omitted groups, and leaves unknown tokens outside the effective snapshot. Catalog validation rejects duplicate tokens, duplicate group defaults, and inconsistent bounds before profile resolution.

The initial MainApplication supplies an empty catalog. Therefore every syntactically valid token is unknown, the effective snapshot is empty, and current behavior cannot change. Later changes must define catalog entries and consumers explicitly.

Alternatives considered:

- Interpret token prefixes directly: rejected because identifier spelling would silently become behavior and make later compatibility difficult.
- Store expanded Boolean flags beside COMP-Nr. values: rejected because duplicated facts can contradict the catalog.
- Scatter direct token checks through application code: rejected because it couples consumers to file syntax and bypasses defaults and unknown-token handling.

### Preserve unknown tokens as opaque forward-compatible data

Unknown tokens remain in the stored snapshot and canonical output. Service-managed add/remove operations start from the complete stored token set, so changing a known token cannot accidentally erase data introduced by newer firmware. Unknown tokens never contribute effective capabilities or behavior. Explicit removal by exact token remains possible.

An unknown token that resembles a future member of a known group is still opaque; the current resolver does not infer group membership from a prefix. Future catalog additions can recognize it after a firmware update. A later hardware-specific change must define a safe default and compatibility behavior for each group it introduces.

### Use staged mutation and restart-scoped activation

The service owns fixed-capacity active and pending snapshots. Mutations add or remove exact tokens or stage a complete bounded token set, then run grammar, duplicate, catalog, and encoded-size validation. Commit persists and verifies the pending record with generation incremented from the newest valid stored record. It sets restart-required state but does not replace the active snapshot.

At next service initialization, the newest valid committed record becomes active. This gives future composition consumers one construction boundary and avoids partially tearing down hardware and software services.

No user interface or transport command is added now. The shared API is the modification boundary for tests and future service/manufacturing adapters; the file remains provisionable by a tool that implements the documented encoding.

### Integrate after persistence without making startup depend on storage

MainApplication initializes the service after `ef800_persistence_init`. Missing files, `-ENOTSUP`, or both slots being invalid produce an empty/default snapshot and a diagnostic, not startup failure. The mechanism does not write a default file automatically, format storage, or touch internal boot metadata.

The module lives with shared persistence-oriented code and has a focused native test suite using `ef800_persistence_fake`. MainApplication integration is limited to initialization and logging because no runtime consumer exists in this change.

## Risks / Trade-offs

- [A future firmware recognizes a token that older firmware preserved but ignored] -> Require each later catalog change to specify safe defaults, compatibility, and its first consumers rather than assigning behavior by prefix.
- [Generation comparison becomes ambiguous after wraparound] -> Use unsigned serial-number comparison, reject an ambiguous half-range difference, and keep the previous valid record.
- [Both slots are manually edited with the same generation but different content] -> Diagnose the conflict and fall back to the empty/default snapshot instead of choosing nondeterministically.
- [Configuration storage is unavailable while USB MSC owns or blocks FAT] -> Continue with defaults; do not move this non-boot-critical mechanism into internal metadata in this change.
- [Text parsing increases malformed-input surface] -> Use fixed buffers, strict ASCII grammar, whole-record rejection, deterministic encoding, and boundary-focused native tests.
- [An API exists before any consumer] -> Keep integration intentionally minimal and document that later behavior requires a separate specification; this prevents examples from becoming accidental requirements.

## Migration Plan

1. Add the shared parser, encoder, catalog resolver, and service with an empty catalog.
2. Add fake-persistence tests, including two-slot recovery and interrupted-write cases.
3. Initialize the service in both physical and native-sim MainApplication builds without adding consumers.
4. Document the file grammar and extension rules.
5. Existing installations without profile files continue with unchanged defaults. Rollback simply ignores the new private files; no existing persistence format is modified.

## Open Questions

None. Concrete COMP-Nr. groups, defaults, factories, provisioning transports, and hardware effects are deliberately deferred to separately specified changes.
