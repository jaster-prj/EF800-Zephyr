## 1. Contract and Registry

- [x] 1.1 Define stable EF800 application, interface, service, plugin-ID, capability, ABI-size, and buffer-limit constants in a public contract export.
- [x] 1.2 Define Measurement, Calculation, optional UI, Port, Workspace, LCD/screen, Keyboard, logging, and clock service tables without Zephyr or driver types in public headers.
- [x] 1.3 Implement the EF800-owned registry for built-in and external descriptors, including duplicate, invalid, incompatible, missing, and unavailable diagnostics.
- [x] 1.4 Register built-in Elta3 and FTS3XX adapters through the same registry path as loaded plugins.
- [x] 1.5 Define and validate EF800-owned hardware-capability metadata in the application/plugin contract.

## 2. Startup Discovery and Storage Boundary

- [x] 2.1 Integrate startup package scanning and validation into EF800 Application only when internal storage is reachable.
- [x] 2.2 Ensure storage-unavailable startup continues with built-in functionality and does not format, overwrite, monitor, or access plugin files.
- [x] 2.3 Preserve existing USB MSD ownership and restart behavior; verify EF800 Application never scans or executes plugin files while EF800 Backup owns storage.
- [x] 2.4 Add malformed, corrupt, incompatible, duplicate, target-mismatch, and valid-package discovery tests.
- [x] 2.5 Add discovery tests for EF800 hardware metadata and port-capability filtering without changing generic PluginManager behavior.

## 3. Measurement Integration

- [x] 3.1 Replace builder-specific EF800 Application measurement dispatch with registry lookup and interface validation.
- [x] 3.2 Preserve port assignment, compatibility matching, UART claims, serial configuration defaults, and release ordering under host ownership.
- [x] 3.3 Add persisted external plugin-ID handling that preserves missing assignments and distinguishes missing plugins from invalid records.
- [x] 3.4 Expose ready Measurement menu entries only for valid assigned plugins with compatible ready routes.
- [x] 3.5 Route optional directional/enter actions through the same active Measurement binding and Port service.
- [x] 3.6 Preserve existing Elta3, FTS3XX, port, UART, persistence, and protocol tests.
- [x] 3.7 Dynamically expose external Measurement plugins in port assignment only when their EF800 hardware requirements match the selected port.

## 4. Calculation Integration

- [x] 4.1 Integrate the bounded workspace snapshot and result-buffer service with production Workspace records.
- [x] 4.2 Load and invoke Calculation plugins without exposing filesystem handles or implicit workspace mutation.
- [x] 4.3 Add explicit preview, validation, confirmation, and atomic persistence handling for plugin results.
- [x] 4.4 Test malformed input, oversized snapshots, oversized output, arithmetic errors, and failed persistence without truncation or partial mutation.

## 5. UI and Keyboard Services

- [x] 5.1 Add declarative menu metadata and optional UI capability handling to the host registry.
- [x] 5.2 Implement the bounded screen model with host-owned header, body limits, footer policy, and explicit overflow handling.
- [x] 5.3 Wrap T6963 operations in a host service without exposing driver, GPIO, DeviceTree, or mutex types to plugins.
- [x] 5.4 Convert existing keyboard output to logical key events for plugin dispatch while retaining raw protocol ownership in EF800 Application.
- [x] 5.5 Implement plugin session lifecycle, consumed/unconsumed keys, bounded text-input mode, back/cancel behavior, and global-key precedence.
- [x] 5.6 Add UI regression tests for menu contributions, header preservation, navigation, keyboard dispatch, input limits, and plugin teardown.

## 6. SDK, Documentation, and Verification

- [x] 6.1 Publish the EF800 contract export, ABI limits, target metadata, and Measurement/Calculation/UI examples for plugin developers.
- [x] 6.2 Add native tests for registry policy, startup storage states, menu visibility, plugin lifecycle, and bounded services.
- [x] 6.3 Add production EF800 Application target acceptance coverage for package discovery, Measurement assignment/readiness, Calculation invocation, plugin UI, and restart-based USB transitions.
- [x] 6.4 Run all four firmware builds and the native suite, then verify image sizes, linked addresses, and generated DeviceTree outputs.
- [x] 6.5 Update `docs/pluginmanager-implementation.md`, `docs/migration-status.md`, and relevant testing documentation when the implementation gates close.

## 7. Deferred Security Gate

- [ ] 7.1 Document and test the production package authenticity, executable-memory, stack, timeout, fault containment, and rollback policy before enabling arbitrary user-uploaded code for release.
