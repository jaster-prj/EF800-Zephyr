## Why

EF800 MainApplication currently presents one fixed measurement action list and treats plugin input as transient host calls. This prevents the selected device from defining its own usable workflow and leaves the existing Elta3 and Fennel1 measurement integrations out of sync with the current plugin implementation. A persistent plugin-owned submenu session is needed so device-specific actions and keyboard behavior are coherent.

## What Changes

- Replace the fixed measurement action menu for plugin-backed devices with a submenu generated from the selected plugin's declared actions.
- Add an Elta3 submenu containing `Measure`, `Up`, `Down`, and `Enter`.
- Add a Fennel1 submenu containing only `Measure`.
- Keep plugin-backed submenu entries dynamic: add them after a compatible assigned device is selected and remove them when the selection/session ends or the device is unavailable.
- Route logical keyboard events received while a plugin submenu is active to that plugin's state machine, while retaining host ownership of global keys and session exit.
- Keep measurement results, save/discard flow, port ownership, and existing non-plugin measurement behavior compatible with the current MainApplication workflow.
- Add native tests for menu composition, selection/session lifecycle, action routing, keyboard dispatch, and unsupported-action handling.

## Capabilities

### New Capabilities

- `measurement-plugin-submenus`: Device-specific measurement submenus and persistent plugin-owned keyboard/navigation state.

### Modified Capabilities

- None. The existing `openspec/specs/` directory contains no established capability spec to modify; the broader `plugin-integration` work is an in-progress change artifact rather than a main specification.

## Impact

- MainApplication menu state, rendering, and keyboard dispatch in `application/ef800-main/src/main.c`.
- Measurement host activation and lifecycle APIs in `application/ef800-main/src/ef800_plugin_host.[ch]` and the measurement plugin contract under `application/plugins/measurement-plugin/include/`.
- Elta3 and Fennel1 plugin descriptors/adapters and their host tests. The repository currently names the second adapter `fts3xx`; the implementation plan must preserve the requested product-facing Fennel1 name or explicitly map it to that existing adapter after confirming the device identity.
- Native test coverage and relevant migration/plugin documentation. No changes to USB ownership, plugin package discovery, or firmware image layout are intended.
