## Why

EF800 already has a PluginManager prototype for package validation, loading, measurement interfaces, port services, and bounded calculation data. EF800 Application still uses fixed measurement/calculation behavior and a host-only menu, so installed plugins cannot extend the product coherently. This change integrates plugin discovery and execution into EF800 Application while preserving the existing restart-based USB MSD/CDC/none ownership model.

## What Changes

- Add startup-only plugin discovery when EF800 Application can reach internal file storage.
- Register built-in and externally installed Measurement and Calculation plugins through one EF800-owned registry and dispatch path.
- Extend the Calculation menu with valid installed calculation plugins.
- Extend the Measurement menu only with valid measurement plugins assigned to compatible, ready physical ports.
- Preserve missing and unavailable assignments as diagnosable states rather than silently remapping them.
- Add a host-controlled plugin UI contract for menu metadata, bounded display content, and logical keyboard events.
- Keep the LCD header, resource ownership, navigation boundary, and USB storage policy owned by EF800 Application.
- Provide bounded workspace and port services without exposing filesystem handles, Zephyr internals, raw UART framing, or direct T6963 access to plugins.
- Require explicit user-controlled operations for workspace mutation and preserve existing USB MSD behavior without hot loading or filesystem monitoring.

## Capabilities

### New Capabilities

- `ef800-plugin-integration`: Startup discovery, lifecycle, registry, interface dispatch, persistence, port-bound Measurement behavior, Calculation behavior, and host-controlled plugin UI integration.

### Modified Capabilities

- None. No existing OpenSpec capability specifications are present in this repository.

## Impact

- EF800 Application integration in `application/ef800-application/src/main.c` and new EF800 plugin contract/registry/service code.
- Shared persistence, port assignment, UART ownership, workspace, and measurement adapters.
- PluginManager contract registration and the public SDK contract export.
- External Measurement and Calculation plugin packages and their validation/lifecycle tests.
- LCD/keyboard integration through host-owned services and logical event dispatch.
- Native, target, malformed-package, persistence, storage-ownership, and firmware build verification.

Out of scope for this change are hot replacement, filesystem monitoring, changes to USB MSD behavior, signed packages, arbitrary file capabilities, plugin-created threads, and production qualification of the external QSPI/FAT hardware.
