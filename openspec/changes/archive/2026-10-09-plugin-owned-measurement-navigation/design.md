## Context

EF800Application currently maintains port-oriented measurement state
(`measurement_port_ids` and `selected_measurement_port`) and has accumulated
plugin-specific branches for names, IDs, and serial defaults. The plugin
registry already carries generic identity, kind, hardware requirement, state,
and declared measurement actions. The plugin host already exposes generic
session rendering, input, measurement, and serial-configuration operations.

The desired workflow makes the plugin the user-facing measurement identity,
while the port remains an internal resource assignment used to provide the
transport. Assignment must configure the transport from plugin-owned metadata
without instantiating plugin code merely to query defaults.

## Goals / Non-Goals

**Goals:**

- Remove plugin-specific IDs, names, protocol defaults, and submenu assumptions
  from EF800Application.
- Add plugin-declared serial defaults to validated plugin metadata or an
  equivalent generic host-facing descriptor contract.
- Display compatible discovered plugins during port assignment.
- Build the Measurement menu from ready assigned plugin entries.
- Resolve selected plugin entries to assigned ports and transports internally.
- Preserve plugin-owned submenu rendering, action routing, measurement result
  handling, and workspace persistence.
- Keep the physical/native-sim boundary and hardware qualification boundary
  unchanged.

**Non-Goals:**

- Changing the plugin wire ABI without updating package descriptors and
  compatibility/version checks.
- Moving UART ownership or raw keyboard framing into plugins.
- Exposing port numbers as the primary Measurement-menu identity.
- Adding instrument-specific behavior to EF800Application as a workaround.
- Replacing the existing workspace file format.

## Decisions

### Plugin metadata owns identity and serial configuration

Extend the versioned plugin metadata contract with bounded serial defaults and
make the host validate the metadata before registering a plugin as ready.
Registry entries will expose the validated values to EF800Application. This is
preferred over calling a plugin instance during assignment because assignment
must be safe before activation and must not depend on native package execution
being available merely to configure a UART.

An alternative is to keep serial defaults in a host-side table, but that
reintroduces the exact plugin knowledge this change removes. Another alternative
is to instantiate plugins during assignment, but that couples configuration to
runtime image loading and caused the current crash path.

### Use plugin-centric measurement selection with an internal port lookup

Maintain an internal assignment table that maps plugin ID to port ID and
transport. Build the user-facing Measurement menu from registry entries whose
assignment is valid and ready. Selecting a menu item stores the plugin ID and
resolves its port only for session transport access.

This preserves port ownership internally while making plugin identity stable
for the user and for future non-serial plugin types.

### Reuse the generic plugin display and action contract

The plugin session display model remains authoritative for submenu title,
labels, row count, footer, and action bitmap. EF800Application provides generic
navigation and workspace integration only; it does not know whether an action
means an ELTA3 command, a Fennel1 request, or another plugin operation.

### Version metadata compatibility explicitly

Adding serial defaults changes the descriptor metadata contract. The metadata
version and size checks will distinguish updated packages from older packages.
Older packages will be marked incompatible rather than partially assigned.

## Risks / Trade-offs

- **[Risk]** Existing packages lack the new serial metadata.  
  **Mitigation:** bump or validate metadata version/size and rebuild all shipped
  packages; reject old packages with a clear registry diagnostic.
- **[Risk]** A plugin can be assigned to multiple ports or a port can be shared
  accidentally.  
  **Mitigation:** keep assignment uniqueness and UART ownership checks in the
  generic port-assignment service.
- **[Risk]** The Measurement menu may become empty after a transport failure.  
  **Mitigation:** derive entries from ready assignments at menu-open time and
  report the existing no-ready-device diagnostic without stale entries.
- **[Risk]** Native-sim image lifetime can corrupt multiple loaded plugins.  
  **Mitigation:** use independent bounded image slots or a tested resident-image
  lifecycle, and add multi-plugin load/session regression coverage.

## Migration Plan

1. Extend metadata structures and plugin descriptors with serial defaults and
   update compatibility/version validation.
2. Rebuild and verify all measurement plugin packages.
3. Refactor assignment and Measurement-menu state to use registry/plugin IDs
   with internal port resolution.
4. Run native-sim and physical-target builds plus plugin, navigation, and
   workspace persistence tests.
5. Deploy fresh packages and clear/reconcile stale incompatible assignments
   through the existing assignment persistence path.

Rollback consists of restoring the previous application and package set
together; mixed metadata versions are not supported.

## Open Questions

- Whether serial defaults belong directly in `ef800_plugin_metadata` or in a
  separate versioned serial-capability extension referenced by that metadata.
- Whether the Measurement menu should preserve assignment order or sort entries
  by plugin-declared display name.
