## Why

EF800 Application currently treats measurement devices as ports and contains
knowledge of specific plugin IDs and names. This prevents plugins from fully
owning their identity, serial configuration, submenu structure, and available
actions, and it makes the LCD workflow fragile when plugins are assigned or
loaded dynamically.

## What Changes

- Make plugin metadata the source of plugin names, compatibility, serial
  defaults, readiness, and declared measurement actions.
- Make configuration port assignment display compatible discovered plugins and
  configure the selected port from plugin-declared metadata.
- Replace the port-oriented Measurement menu with a plugin-oriented menu that
  lists available assigned plugins such as `ELTA3` and `Fennel1`.
- Resolve the selected plugin to its assigned port and initialized transport
  only when opening its submenu or executing an action.
- Render and execute plugin-owned submenu actions through the existing generic
  display and input contracts.
- Remove ELTA3/Fennel1 IDs, names, protocol defaults, and submenu assumptions
  from EF800 Application.
- Preserve workspace measurement persistence after a plugin-owned measurement
  action completes.

## Capabilities

### New Capabilities

<!-- None. This extends the existing Virtual EF800/EF800 Application capability. -->

### Modified Capabilities

- `virtual-ef800-platform`: Measurement navigation and plugin assignment are
  driven by discovered plugin metadata and plugin-owned submenu actions rather
  than port-specific EF800 Application knowledge.

## Impact

- `application/ef800-application/src/main.c` for generic assignment and plugin-based
  measurement navigation.
- Plugin descriptor metadata and registry structures for serial defaults and
  plugin-owned action information.
- `application/ef800-application/src/ef800_plugin_host.c` and host APIs for exposing
  validated metadata and assigned-plugin session resolution.
- Measurement plugin descriptors/packages and their verification tests.
- Virtual EF800, native-sim, persistence, and end-to-end measurement tests.
