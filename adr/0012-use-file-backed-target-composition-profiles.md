# Use File-Backed Target Composition Profiles

- Status: accepted
- Date: 2026-10-28
- Supersedes: None

## Context

EF800 targets need a durable way to identify approved combinations of hardware and software behavior without embedding target-specific conditionals throughout EF800Application. Composition selections must remain serviceable in the existing private configuration storage, tolerate older and newer firmware, survive interrupted updates, and provide a stable construction boundary for future hardware-specific factories. The initial mechanism must not assign meaning to example component identifiers or alter current target behavior.

## Decision

EF800 will represent target composition as a bounded, versioned, checksummed file profile containing one space-separated `composition=` list of COMP-Nr. tokens. A repository-owned immutable catalog will resolve recognized tokens and defaults; syntactically valid unknown tokens will be preserved but will not affect the effective composition.

The logical profile will use two recoverable configuration-file slots selected by valid generation so an interrupted update retains a complete predecessor. Changes will be staged and verified during the current boot but will become active only when the composition service initializes after a restart. The initial catalog will be empty and no runtime subsystem will consume composition until a later specified change adds concrete definitions and behavior.

## Consequences

- Future target variants can add stable COMP-Nr. catalog entries and service factories without changing the persisted profile model.
- Older firmware can preserve newer identifiers without activating unknown behavior.
- Composition updates require a controlled restart and do not hot-reconfigure hardware or software services.
- The QSPI/FAT configuration root must be available to activate a persisted profile; unavailable or invalid storage falls back to registered defaults.
- Two physical records implement one logical profile and require deterministic generation, conflict, checksum, and read-back rules.
- Hardware-specific meanings, safe defaults, resource conflicts, UI effects, and assignment reconciliation remain separate decisions for later changes.
