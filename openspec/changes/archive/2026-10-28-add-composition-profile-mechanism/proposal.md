## Why

EF800 currently has no durable, firmware-readable identity for the target's assembled software/hardware composition. A generic profile mechanism is needed before individual hardware differences are defined, so later changes can add stable COMP-Nr. meanings without scattering target-specific conditionals through the application.

## What Changes

- Add a versioned target-profile file in the existing configuration persistence root with one space-separated `composition=` line.
- Add a bounded parser and encoder for opaque COMP-Nr. tokens, including generation and integrity validation.
- Add a repository-owned catalog/resolver interface that can supply defaults for omitted groups while preserving and ignoring unknown tokens for forward and backward compatibility.
- Add a composition service that exposes stored and effective snapshots and supports staged, validated, restart-scoped profile replacement.
- Start with no hardware-specific COMP-Nr. definitions or consumers, so a missing, empty, or unavailable profile preserves current application behavior.
- Add native tests for parsing, bounds, defaults, unknown-token preservation, invalid records, modification, read-back verification, and restart-scoped activation.
- Explicitly defer LCD, Bluetooth, battery, port, plugin-assignment, menu, pin-control, and other composition-specific behavior to later changes.

## Capabilities

### New Capabilities
- `target-composition-profile`: Defines generic file persistence, parsing, default resolution, unknown-token compatibility, modification, and activation semantics for target COMPOSITION profiles.

### Modified Capabilities

None.

## Impact

- Adds a shared composition-profile/service module and native test coverage.
- Uses the existing `EF800_PERSISTENCE_CONFIGURATION` abstraction and its fake and Zephyr backends; no new storage partition is introduced.
- MainApplication will initialize the generic service after persistence setup, but the resolved composition will have no runtime consumers in this change.
- Documentation will describe the profile format and extension contract for future COMP-Nr. catalogs and factories.
