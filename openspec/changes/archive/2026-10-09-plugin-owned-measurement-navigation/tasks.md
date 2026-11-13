## 1. Plugin Metadata and Registry Contract

- [x] 1.1 Extend the versioned plugin metadata contract with validated serial-default fields and a compatibility/version rule.
- [x] 1.2 Add serial-default fields to the plugin registry descriptor information and populate them from package metadata.
- [x] 1.3 Update ELTA3 and Fennel1 descriptors/packages to declare their serial defaults and rebuild package verification fixtures.
- [ ] 1.4 Add tests proving valid metadata is accepted, stale metadata is rejected as incompatible, and plugin names/actions remain package-owned.

## 2. Generic Port Assignment

- [x] 2.1 Refactor port-assignment filtering to use registry plugin kind, readiness, hardware capabilities, and declared names instead of plugin-specific IDs.
- [x] 2.2 Configure and initialize a selected port from registry serial metadata without instantiating or executing the plugin.
- [x] 2.3 Persist plugin-to-port assignment and preserve generic UART ownership/reconciliation behavior for physical and native-sim targets.
- [ ] 2.4 Add assignment tests for compatible plugins, incompatible ports, unavailable plugins, duplicate ownership, and transport initialization failures.

## 3. Plugin-Owned Measurement Navigation

- [x] 3.1 Replace port-oriented Measurement-menu entries with ready assigned plugin entries using registry names and stable plugin IDs.
- [x] 3.2 Resolve a selected plugin entry to its assigned port and initialized transport internally while keeping port IDs out of the user-facing identity.
- [x] 3.3 Render plugin-owned submenu titles, rows, footers, and action bitmaps through the generic session contract.
- [x] 3.4 Route selected plugin actions and logical input without instrument-specific EF800Application branches, then close sessions safely.
- [ ] 3.5 Add navigation tests covering assignment, plugin selection, submenu entry, measure action, and no-ready-plugin behavior.

## 4. Measurement Persistence and Regression Coverage

- [x] 4.1 Preserve the existing measurement editor and append successful plugin results to the selected workspace file using generic result data.
- [ ] 4.2 Add an end-to-end native-sim scenario for assigning ELTA3 to Port 1, selecting the plugin from Measurement, executing Measure, and verifying the workspace entry.
- [ ] 4.3 Add a parallel Fennel1 scenario to verify that plugin-specific protocol behavior remains inside the package.
- [ ] 4.4 Run PluginManager host tests, plugin package verification, native-sim builds, virtual gateway tests, and the complete LCD/keyboard workflow.
