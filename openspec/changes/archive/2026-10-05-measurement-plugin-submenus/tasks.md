## 1. Confirm Contracts And Device Identity

- [x] 1.1 Confirm whether the requested Fennel1 device is represented by the existing `fts3xx` adapter; document the product-facing name, plugin ID, protocol, and expected measurement behavior.
- [x] 1.2 Define bounded measurement action identifiers, action metadata, consumed/unconsumed keyboard results, and plugin-session lifecycle states in the measurement/plugin host contract.
- [x] 1.3 Define the final logical keyboard mapping and host precedence rules for submenu navigation, global keys, and back/cancel.

## 2. Implement Plugin Session And Host APIs

- [x] 2.1 Add a host-owned measurement plugin session that binds one selected plugin instance to one assigned ready transport and rejects concurrent or stale sessions.
- [x] 2.2 Add declarative action metadata and capability validation so only actions implemented by the selected plugin can appear in its submenu.
- [x] 2.3 Add persistent session APIs for action dispatch, logical keyboard handling, error reporting, and deterministic teardown; retain compatibility wrappers for existing one-shot measurement calls where needed.
- [x] 2.4 Update Elta3 to declare and execute `Measure`, `Up`, `Down`, and `Enter` through the session contract.
- [x] 2.5 Update the confirmed Fennel1 implementation to declare only `Measure` and return a deterministic unsupported result for directional actions.

## 3. Integrate EF800 Application Menus And Keyboard Routing

- [x] 3.1 Replace the plugin-backed branch of the fixed measurement action list with a dynamically rendered submenu based on the active plugin action metadata.
- [x] 3.2 Create and destroy the plugin submenu session when a ready assigned device is selected, exited, invalidated, or its route becomes unavailable.
- [x] 3.3 Route `Measure` through the existing measurement-record/save/discard flow while keeping plugin session and transport ownership correct.
- [x] 3.4 Route Elta3 directional actions through the selected plugin session without claiming another UART or invoking the legacy adapter.
- [x] 3.5 Dispatch decoded logical keyboard events to the active plugin state machine after host global-key and back/cancel handling, and apply the documented fallback for unconsumed keys.
- [x] 3.6 Preserve the existing non-plugin measurement action workflow and verify that plugin submenu state cannot leak into parent menus or another selected device.

## 4. Test And Document

- [x] 4.1 Add native contract tests for action metadata, exact Elta3/Fennel1 action sets, unsupported actions, session lifecycle, and single-transport ownership.
- [x] 4.2 Add EF800 Application/state-machine tests for dynamic menu creation/removal, selection reset, keyboard consumption, global-key precedence, teardown, and fallback navigation.
- [x] 4.3 Add regression tests for measurement result creation, pending save/discard behavior, legacy device dispatch, and route/plugin unavailability.
- [x] 4.4 Update measurement-plugin SDK and migration documentation with the submenu/session contract and the confirmed Fennel1 identity.
- [x] 4.5 Run the measurement plugin host tests, the native test suite, and all required firmware builds; record results and verify no image-layout or USB-ownership regressions.
