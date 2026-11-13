# Organize EF800 Firmware by Domain Ownership

- Status: accepted
- Date: 2026-11-11
- Supersedes: None

## Context

Several EF800 firmware entry points contain multiple workflows, state machines, and hardware interactions. Large files make it difficult to identify mutable-state ownership and review behavior changes. The images differ in MCU, memory layout, and peripheral ownership, so similarly named applications do not imply that their implementations should be merged into one shared module. EF800Application additionally has one physical/native-sim composition and package/host ownership boundaries that must remain intact.

## Decision

EF800 firmware shall organize behavior into cohesive domain modules with explicit state, resource, and interface ownership. Image entry points serve as composition roots for target-specific hardware, Zephyr services, startup ordering, and domain initialization. Shared modules are used for genuinely common policies, data contracts, or behavior, not created to mirror firmware-image names.

Refactoring shall proceed in behavior-preserving increments. Per-domain structures and interfaces are used when they clarify ownership or enable meaningful tests; unnecessary indirection and monolithic context objects are avoided. Complex functions are decomposed along responsibility and state-transition boundaries rather than by an arbitrary source-line threshold. Existing specifications and architecture decisions continue to define externally observable behavior and resource ownership.

## Consequences

- Firmware entry points should become smaller composition boundaries while domain code remains independently reviewable.
- Source manifests, include paths, and target-specific adapters must make image ownership explicit.
- Domain modules may expose meaningful seams for native tests, but splitting a function alone does not justify a test.
- EF800Application physical and native-sim builds must continue to use the same production composition and shared source manifest.
- Refactoring must preserve plugin host ownership, boot/update safety, storage/USB exclusivity, and all other existing compatibility contracts.
