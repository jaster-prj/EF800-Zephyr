## Context

Production `ef800-application` owns the PluginManager integration, package discovery,
port policy, Workspace policy, UI, and keyboard session. Generic PluginManager
tests remain in the PluginManager repository; the former NUCLEO prototype is
not part of the EF800 product test surface.

The device already separates USB MSD ownership into the EF800 Backup. Changing USB selection restarts the application, so a restart is an acceptable and intentional synchronization boundary. EF800 Application only needs to discover plugins during a boot where internal storage is reachable. Hot replacement and filesystem monitoring are not part of this design.

## Goals / Non-Goals

**Goals:**

- Integrate built-in and external Measurement and Calculation plugins into EF800 Application.
- Discover packages once during startup when storage is available.
- Keep port assignment, UART claiming, persistence, workspace policy, LCD header, keyboard protocol, and navigation owned by EF800 Application.
- Let plugins contribute menu metadata, bounded display content, and logical keyboard behavior.
- Preserve diagnosable missing/unavailable assignments and existing USB MSD behavior.
- Provide a public EF800 contract suitable for SDK generation without exposing Zephyr or driver internals.

**Non-Goals:**

- Hot loading, hot replacement, or filesystem monitoring.
- Any change to the current USB MSD/CDC/none restart and storage ownership behavior.
- Signed packages, anti-rollback, production executable-memory protection, or QSPI qualification.
- Plugin-created threads, direct filesystem access, arbitrary file capabilities, or direct pixel drawing.

## Decisions

### Startup-only discovery

EF800 Application will mount/use the already available internal storage during startup, scan the configured plugin location, validate packages, and construct an in-memory registry. If storage is unavailable, the registry contains built-ins only for that boot. A later USB mode transition naturally causes a restart and a fresh scan.

This is preferred over hot replacement because the existing product lifecycle already restarts across MSD ownership changes. It avoids concurrent file mutation, plugin unload races, and a second storage state machine.

### One registry with interface-specific policy

Built-in adapters and external descriptors use one registry and dispatch path. The registry records interface type, stable plugin ID, package/version metadata, capabilities, and availability diagnostics.

Measurement and Calculation use different activation policy:

```text
Calculation:  valid package -> menu contribution
Measurement:  valid package + persisted port assignment
              + compatible route + ready UART -> operational menu entry
```

The host remains responsible for resolving assignments, claiming UARTs, applying serial defaults/configuration, and passing a Port service. Measurement plugins never claim UARTs directly.

### EF800-owned hardware requirements

Hardware requirements are part of the EF800 application contract, not the generic PluginManager. An EF800 Measurement descriptor exposes an EF800 metadata extension containing a required hardware-capability bitmask. PluginManager validates and loads the generic package but does not interpret UART, SPI, I2C, USB, or flow-control semantics.

EF800 Application reads the EF800 metadata before showing a plugin in port assignment and applies:

```text
(port.capabilities & plugin.required_hardware) == plugin.required_hardware
```

This keeps generic package loading reusable across applications while allowing EF800 to evolve its hardware policy independently.

### Optional UI capability on each plugin interface

PluginManager v1 describes one interface per descriptor. Instead of requiring one package to export multiple independent interfaces, Measurement and Calculation interfaces may advertise an optional UI capability. This preserves the one-interface descriptor model while allowing a plugin to provide menu metadata and an active screen.

Alternatives considered:

- Multiple interfaces per package: more expressive, but requires a generic ABI/versioning decision and new loader tests.
- A separate UI plugin paired by plugin ID: flexible, but creates pairing, lifecycle, and persistence complexity.
- Optional UI capability: fits the existing ABI direction and keeps activation ownership simple.

### Retained bounded screen model

The UI service will expose declarative menu metadata and a bounded screen model rather than a raw T6963 drawing API. The host renders the fixed header and plugin content within a bounded body region.

```text
row 0       host-owned header
rows 1..6   plugin screen model
row 7       host-owned navigation/status policy
```

The model can contain a title, bounded text rows, selected row/input state, and status/footer hints. It does not expose `struct ef800_t6963`, GPIOs, DeviceTree objects, or arbitrary pixel operations.

### Logical keyboard dispatch

The existing keyboard parser remains the only raw UART protocol consumer. It converts controller output to logical key events. EF800 Application dispatches events to the active plugin session and retains global system keys, session lifetime, and fallback navigation. A plugin can explicitly enter bounded text-input or modal mode, but cannot create an independent keyboard thread.

### Explicit workspace mutation

Calculation plugins receive bounded workspace snapshots and bounded output buffers. Filesystem reads, workspace serialization, validation, confirmation, and atomic persistence remain host adapters. Plugin output is never silently truncated and is not appended implicitly.

## Risks / Trade-offs

- [External package code is not yet production-secure] -> Keep package trust, executable-memory protection, stack/timeout/fault policy, and anti-rollback as explicit verification gates rather than implying this change closes them.
- [A plugin can consume too much UI time] -> Serialize calls through EF800 Application, define bounded buffers and call contracts, and defer timeout/fault enforcement to the production execution-safety work.
- [Static eight-row LCD limits plugin screens] -> Use a bounded text model first; reject oversized metadata/content explicitly and avoid arbitrary graphics.
- [Persisted external IDs outlive package files] -> Preserve assignments and distinguish missing, invalid, incompatible, and unavailable states; never silently map unknown IDs to built-ins.
- [Built-in and external behavior diverge] -> Register built-in Measurement adapters through the same registry and dispatch path used by loaded plugins.
- [Storage availability differs across boots] -> Treat discovery as boot-scoped and leave existing MSD ownership and restart behavior unchanged.

## Migration Plan

1. Export and stabilize EF800 contract IDs, ABI limits, service tables, and SDK examples.
2. Integrate the registry and startup scan into EF800 Application while retaining built-in behavior as adapters.
3. Migrate Measurement dispatch and persistence to registry/plugin-ID policy.
4. Integrate Calculation bounded workspace invocation and explicit result handling.
5. Add host UI and keyboard services, then migrate plugin menu/session behavior without changing the host header.
6. Validate startup with reachable and unreachable storage, USB MSD isolation, malformed packages, missing assignments, native tests, all firmware builds, and image/layout checks.

Rollback is boot-safe: if scanning or plugin registration fails, EF800 Application continues with built-in functionality and no external plugin entries. Individual invalid packages are rejected without blocking valid packages. Existing USB MSD behavior remains unchanged throughout migration.

## Open Questions

- What production package authenticity and executable-memory policy will be required before arbitrary user-uploaded code is enabled?
- What exact plugin storage path and package suffix should be fixed for the production EF800 FAT volume, given the prototype's root-volume short-name limitation?
- Should unavailable assigned Measurement entries appear only in diagnostics/port assignment or also as disabled rows in the operational Measurement menu?
- What exact limits apply to plugin UI title length, row count, row width, input length, and callback duration?
