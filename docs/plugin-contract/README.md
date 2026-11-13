# EF800 Plugin Contract

This directory is the source contract for EF800 plugins. It is consumed by an
external SDK builder and is not a generated SDK vendor directory.

## Public Headers

- `shared/ef800_plugin/ef800_plugin_contract.h`
- `shared/ef800_plugin/ef800_plugin_services.h`
- `shared/ef800_plugin/ef800_plugin_screen.h`
- `shared/ef800_plugin/ef800_calculation_service.h`
- `application/plugins/measurement-plugin/include/ef800_measurement_plugin.h`
- `application/plugins/measurement-plugin/include/ef800_port_service.h`

The public contract contains no Zephyr, DeviceTree, GPIO, T6963, filesystem,
or persistence types.

## Identity

The application UUID and interface UUID byte definitions are in
`ef800_plugin_contract.h`. Interface versioning is independent per interface.
PluginManager remains generic and only carries opaque descriptor extension
bytes. EF800 owns the metadata extension UUID and schema.

## Plugin Kinds

### Measurement

Measurement plugins represent external devices. EF800Application supplies a
host-owned Port service and controls UART claiming, serial configuration,
assignment, readiness, lifecycle, and release.

Measurement metadata declares required EF800 hardware capabilities. A plugin is
eligible for a port only when all required bits are present:

```text
(port.capabilities & plugin.required_hardware) == plugin.required_hardware
```

### Calculation

Calculation plugins do not require a physical port. EF800Application supplies a
bounded Workspace snapshot and a bounded result buffer. Plugins cannot access
filesystem handles or mutate Workspace state implicitly.

## UI Contract

UI is an optional capability of the primary plugin interface. The host owns:

- LCD header row.
- Footer/navigation row.
- Raw keyboard UART protocol.
- Plugin session lifetime.
- Global/system key handling.

Plugins provide bounded `ef800_plugin_screen` content for body rows only and
receive logical `ef800_plugin_key_event` values. Direct T6963 and GPIO access
is not part of the contract.

## Limits

The authoritative constants are in `ef800_plugin_contract.h`, including:

- 40 display columns.
- 6 plugin body rows.
- 32-character bounded input.
- 4096-byte Workspace snapshot.
- 4096-byte Calculation result.
- Descriptor name/version limits.

Implementations must reject overflow and must never silently truncate plugin
input, output, screen content, or Workspace snapshots.

## Lifecycle

Plugin packages are discovered at EF800Application startup only when internal
storage is reachable. USB MSD ownership is unchanged; changing USB mode
restarts the application and provides the package update boundary.

The native-sim browser deployment provides an additional development-only
package update boundary: a native-sim `.PMP` can be uploaded through the local
web gateway, written atomically to the virtual FAT `/plugins` directory, and
activated only after the user explicitly restarts the simulator. Native-sim
packages use a host-native ELF target and are behavioral evidence only; they do
not qualify physical ARM package loading, MPU containment, signatures, or
production package security.

The host lifecycle is:

```text
discover -> validate -> register -> load -> create -> invoke
         -> destroy -> unload
```

Invalid packages are rejected before execution. Missing assigned plugins remain
diagnosable and are not remapped to built-in devices.
