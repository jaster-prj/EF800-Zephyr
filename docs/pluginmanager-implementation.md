# EF800 PluginManager Integration Steps

## Current Status (2026-10-04)

Production `ef800-application` is the only EF800 PluginManager host. Generic
PluginManager unit tests remain in the PluginManager repository.

### Proven

- PluginManager is consumed as the pinned Zephyr module; EF800 does not fork
  the generic ABI, package parser, or ELF loader.
- Both target packages are generated, CRC-verified, discovered from the FAT
  volume root, validated through `pm_package_parse_reader()`, and loaded with
  `pm_manager_load_available()`.
- EF800Application loads, instantiates, executes, destroys, and unloads external
  plugins through the generic PluginManager lifecycle.
- ELTA3 measurement passes at `1200 7O2`; Fennel-compatible FTS3XX measurement
  passes at `9600 8N1`.
- Switching between ELTA3 and FTS3XX without reboot passes. The application
  resets USART state, prescaler, and clock policy for every plugin; plugins
  provide the communication parameters only.
- Calculation invocation is separated into bounded application-domain buffers;
  filesystem access and atomic file replacement remain adapters.

### Not Proven or Not Production-Ready

- Production external plugin IDs, assignments, Workspace snapshots, LCD,
  keyboard, menu, and USB ownership boundaries are integrated.
- The external NOR/FAT setup is bring-up configuration, not production
  qualification.
- Native target plugins execute from RAM with MPU disabled. Packages are CRC
  checked but not authenticated.

For controlled hardware bring-up only, `application/ef800-application/plugin-dev.conf`
is an explicit development overlay that disables the ARM MPU so the current RAM
loader can execute locally built ELTA3/FTS3XX packages. It removes MPU and MPU
stack-guard protection and accepts unsigned executable packages; never use this
overlay in a production image or with untrusted package files. Build the normal
EF800Application without the overlay to retain the production configuration.

### Feature-Complete EF800 Gate

EF800 integration is complete only when:

1. EF800Application registers the EF800 contract and owns the complete plugin
   lifecycle.
2. Built-in and external Device plugins use one registry and dispatch path.
3. Calculation plugins receive bounded workspace snapshots and cannot access
   filesystem handles or mutate workspace state implicitly.
4. External IDs, assignments, serial defaults, missing-plugin states, and legacy
   migration are persisted and diagnosable.
5. FAT mounting, package scanning, plugin execution, and workspace access are
   blocked while BackupApplication owns storage through USB MSC.
6. LCD and keyboard services expose logical APIs serialized by EF800Application.
7. Package authenticity, target compatibility, anti-rollback, atomic update,
   rollback, and recovery behavior are implemented.
8. Executable memory, MPU/W^X, stack, timeout, and fault behavior are defined
   and tested on the production MCU.
9. Native, target, malformed-package, power-loss, concurrency, protocol,
   hardware-in-the-loop, and four-firmware verification gates pass.

This document defines the EF800 work required to consume the reusable
PluginManager framework. EF800 owns the application contract and service
implementations. The external framework owns package loading and generic ABI
machinery.

## Target Boundary

```text
External PluginManager
  generic ABI, package parser, ELF loader, registry, SDK builder

EF800Application
  Device interface
  Calculation interface
  Port service
  Workspace service
  LCD and keyboard services
  EF800 IDs and persistence behavior
```

EF800 must not fork or duplicate the generic ELF loader. The EF800 code should
register its contract with the framework and adapt existing application APIs
to the framework service tables.

## EF800 Contract Layout

Create an application-specific contract area, preferably under:

```text
application/ef800-application/src/ef800_plugin/
  ef800_plugin_contract.h
  ef800_plugin_contract.c
  ef800_device_plugin.h
  ef800_calculation_plugin.h
  ef800_plugin_services.h
  ef800_plugin_services.c
  ef800_plugin_registry.c
```

The public contract headers must not expose Zephyr types or internal EF800
structures. They are the input to SDK generation and must be usable by an
external plugin developer.

## Contract Identity

Define stable EF800 identifiers for:

- Application contract.
- Device interface.
- Calculation interface.
- Port service.
- Workspace service.
- LCD service.
- Keyboard service.
- Logging and clock services.

Legacy measurement builder IDs are no longer assignable. Version-1 metadata
migration clears IDs `2` and `3`; current assignments use full external plugin
IDs in version-2 metadata and are retained only while the plugin is loaded.

```text
EF800_PORT_BUILDER_ELTA3  = 2 (legacy, retired)
EF800_PORT_BUILDER_FTS3XX = 3 (legacy, retired)
```

Reserve a documented range for externally installed device plugins. Existing
port assignment records must distinguish an unavailable plugin from a corrupt
assignment and must not silently map an unknown plugin to a built-in device.

## Device Interface

Define a structured result based on the existing measurement representation:

```c
struct ef800_measurement_result {
    int32_t distance_mm;
    int32_t horizontal_gon_1e4;
    int32_t vertical_gon_1e4;
    uint8_t control;
};
```

The device interface should provide at least:

```c
int (*measure)(void *instance,
               struct ef800_measurement_result *result);
```

Optional device commands can support Elta3 operations such as up, down, and
enter. Device plugins return structured values; EF800Application formats values
for workspace output or display.

## Elta3 and FTS3XX Adaptation

The existing protocol implementation is already transport-independent:

```text
ef800_measurement_transport
  -> Port service adapter
  -> Elta3 or FTS3XX protocol implementation
```

Retain the existing behavior and tests:

```text
Elta3: 1200 baud, 7 data bits, odd parity, 2 stop bits
FTS3XX: 9600 baud, 8 data bits, no parity, 1 stop bit
```

Wrap `ef800_elta3_measure()` and `ef800_fts3xx_measure()` as built-in EF800
device plugins before loading external device plugins. This provides a
reference implementation for the SDK and removes protocol dispatch from
`application/ef800-application/src/main.c`.

## Port Service

The Port service adapts the existing transport callbacks:

```text
shared/ef800_measurement/ef800_measurement.h
shared/ef800_measurement/ef800_measurement_zephyr.c
shared/ef800_ports/ef800_port_uart.c
```

The PluginManager must not let plugins claim UARTs directly. EF800Application
must continue to:

1. Load persisted port assignments.
2. Resolve the physical UART route.
3. Check UART readiness.
4. Claim the UART through `ef800_port_uart_claim()`.
5. Load persisted serial configuration.
6. Apply plugin defaults when no configuration exists.
7. Construct the Port service.
8. Create the plugin instance.

Port release must happen after plugin destruction and before a console or USB
ownership transition.

## Calculation Interface

Calculation plugins receive workspace content, not filesystem handles:

```c
struct ef800_calculation_input {
    const char *workspace_text;
    size_t workspace_text_length;
};

struct ef800_calculation_output {
    char *text;
    size_t capacity;
    size_t length;
};
```

The initial contract should support:

- One bounded workspace snapshot.
- One bounded result buffer.
- Explicit output length.
- No implicit workspace mutation.

EF800Application decides whether output is displayed, appended, or rejected.
This decision can later be exposed through the new menu system.

## Workspace Service

Extend the workspace layer with a bounded snapshot operation. Do not expose
`struct ef800_workspace`, `struct ef800_persistence`, or `struct fs_file_t` to
plugins.

Relevant existing code:

```text
shared/ef800_persistence/ef800_workspace.h
shared/ef800_persistence/ef800_workspace.c
shared/ef800_measurement/ef800_measurement.c
```

The service should provide:

```text
snapshot current workspace into caller buffer
append validated text through EF800Application
report selected workspace and available size if required
```

Return `-EFBIG` for an oversized snapshot. Never silently truncate input or
plugin output.

Workspace access must be rejected while BackupApplication owns the storage
through USB MSC.

## LCD and Keyboard Services

Reserve the service IDs now, but implement them after Device and Calculation
plugins work.

The LCD service must wrap the T6963 implementation rather than expose:

```text
struct ef800_t6963
GPIO descriptors
Zephyr mutexes
DeviceTree objects
```

The Keyboard service should expose logical key events, not raw UART framing.
UI operations must be serialized by EF800Application and must not allow plugins
to create independent UI threads.

Relevant existing code:

```text
shared/ef800_drivers/ef800_t6963.h
shared/keyboard_protocol/
application/ef800-application/src/main.c
```

## Plugin Discovery and Storage

Use one configured directory, for example:

```text
/plugins/
```

The `.pmp` suffix is only a candidate-file filter. The manifest and
loaded descriptor define the interface type.

EF800Application controls scanning because storage ownership is application
state. Do not scan or execute plugins while USB MSC owns the QSPI disk.

Initial lifecycle:

```text
mount FAT
  -> scan /plugins
  -> validate manifests
  -> register available plugins
  -> expose plugins to port/menu logic

USB MSC requested
  -> stop plugin operations
  -> destroy/unload active plugins
  -> close workspace resources
  -> unmount FAT
  -> transfer storage ownership
```

Require a rescan or reboot after a user uploads a new package. Do not add hot
replacement until the storage and lifecycle behavior is proven.

## EF800Application Integration

Replace builder-specific branches in:

```text
application/ef800-application/src/main.c
```

with PluginManager calls. The application flow should become:

```text
assignment builder ID
  -> EF800 plugin registry lookup
  -> plugin interface validation
  -> port/service setup
  -> plugin instance creation
  -> Device.measure()
  -> structured result formatting and workspace handling
```

Built-in Elta3 and FTS3XX descriptors must be registered through the same
interface adapter used by loaded plugins. This avoids having two dispatch
models.

## Persistence Changes

Audit these areas before enabling external plugin IDs:

```text
shared/ef800_update/ef800_update.h
shared/ef800_update/ef800_update.c
shared/ef800_ports/ef800_ports.c
shared/ef800_persistence/ef800_persistence_legacy.c
```

Required behavior:

- Preserve built-in IDs and legacy migration behavior.
- Validate external IDs through the plugin registry or reserved-range policy.
- Preserve assignments when a plugin file is missing.
- Report a missing plugin distinctly from invalid persisted data.
- Avoid legacy serial-kind assumptions for external plugins.
- Store external plugin serial configuration under a versioned plugin ID.

## SDK Contract Export

Add an EF800 contract export directory that the external SDK builder consumes:

```text
docs/plugin-contract/              contract description
application/ef800-application/plugin-sdk/ public contract inputs, if generated here
```

The export must contain:

- Application UUID and version.
- Device interface definition.
- Calculation interface definition.
- Public service vtables.
- Stable interface and service IDs.
- ABI limits and buffer sizes.
- Target architecture and toolchain metadata.
- Examples of valid plugin descriptors.

Generated SDK output should not be committed into EF800Application unless the
project deliberately vendors a release. The source contract remains the
authority.

## EF800 Implementation Phases

Current status as of 2026-10-04:

- Generic framework host, ARM package, SDK-builder, generated harness, and
  Zephyr native-sim verification pass.
- Production `ef800-application` is the sole EF800 PluginManager host and clean-builds.
- Built-in Elta3 and FTS3XX dispatch is implemented through the EF800-owned
  static device-plugin registry with persisted IDs unchanged.
- PluginManager is pinned as a west module and production EF800Application
  integrates package discovery, UART ownership, Workspace snapshots, bounded
  Calculation, UI sessions, and hardware-capability filtering.
- Generic PluginManager tests remain in the PluginManager repository. The former
  NUCLEO prototype is removed from EF800.

Decision: name the port-bound action interface **Measurement**. The EF800 host
owns port capability matching, assignment, claims and UART lifetime; the
plugin receives a Port service. Calculation does not require a port and receives
bounded snapshots. An assigned measurement action is shown ready only if the
package exists and its assigned route is ready; otherwise it is shown disabled
with a reason. UART readiness is not instrument presence. One selected binding
controls both measurement and optional directional/enter actions. PluginManager
v1 describes exactly one interface per descriptor, so a package exporting two
distinct interfaces requires a versioned generic ABI decision and tests before
implementation. EF800 boot metadata currently packs builder IDs into four
bits; external IDs require a separate persistence design.

### Phase A: Framework Integration

1. Keep the pinned PluginManager west module and NUCLEO CI build passing.
2. Confirm the module builds without EF800 contract headers.
3. Register the EF800 application contract from EF800Application.
4. Add contract identity and service ID definitions.

### Phase B: Built-in Device Plugins

1. Implement EF800 Device interface.
2. Wrap Elta3.
3. Wrap FTS3XX.
4. Register both through PluginManager.
5. Replace hard-coded measurement dispatch.
6. Preserve all existing native protocol tests.
7. Preserve UART ownership and serial persistence behavior.

### Phase C: Workspace and Calculation Plugins

1. Add bounded workspace snapshot.
2. Add controlled append operation.
3. Implement EF800 Calculation interface.
4. Add a native fake calculation plugin.
5. Test malformed input, oversized input, and oversized output.
6. Add application invocation hooks without menu integration.

### Phase D: Target Package Loading

1. Enable the external package directory after FAT validation gates pass.
2. Add manifest scanning.
3. Validate target ELF packages.
4. Register available external plugins.
5. Test missing, corrupt, incompatible, and duplicate packages.
6. Require controlled rescan/reboot after installation.

### Phase E: Menu and UI

1. Add LCD and Keyboard services.
2. Define menu actions for plugin discovery and execution.
3. Display plugin name/version and unavailable states.
4. Require user confirmation before workspace mutation.
5. Test USB MSC transitions during and after plugin use.

## Verification Gates

Current verification baseline:

- EF800Application, BackupApplication, Bootloader, and keyboard-controller
  pristine builds pass.
- `scripts/verify-images.sh` passes the fixed flash layout:
  bootloader `0x08000000`, backup `0x08010000`, main `0x08030000`, keyboard
  `0x08000000` on the L431 target.
- Native suite: 46/46 test cases passed.
- Generic PluginManager host tests: 3/3 tests passed.
- EF800 contract export: `docs/plugin-contract/`.
- Security policy: `docs/plugin-contract/security-policy.md`. Arbitrary
  user-uploaded executable plugins remain release-disabled until signatures,
  MPU/W^X, stack, timeout, fault-containment, and rollback evidence pass.

The contract export is maintained under `docs/plugin-contract/`. Production
EF800Application target acceptance is documented under
`application/ef800-application/tests/`; the smoke test is:

```powershell
powershell -ExecutionPolicy Bypass -File application/ef800-application/tests/acceptance.ps1 -Port COM7
```

It validates production EF800Application startup discovery and registry/calculation diagnostics without
formatting or replacing storage. Exit code `2` means the internal storage is
unavailable and is an explicit hardware blocker.

Before considering the integration complete:

- Elta3 and FTS3XX native protocol tests pass unchanged.
- All four firmware builds remain successful.
- Port assignment and UART ownership tests pass.
- Missing plugin assignments are preserved and diagnosable.
- Calculation plugins cannot access filesystem handles.
- Workspace snapshots never silently truncate.
- Plugin packages are rejected before execution when invalid.
- EF800Application does not access FAT while USB MSC owns storage.
- Plugin load, create, execute, destroy, and unload paths are tested.
- `docs/migration-status.md` is updated when an implementation phase closes.

## Deferred EF800 Work

- Signed plugin packages.
- Plugin configuration persistence.
- Hot replacement.
- Arbitrary file capabilities.
- Plugin-created threads.
- Automatic result append without user confirmation.
- Production qualification of QSPI/FAT plugin storage.
