# Measurement Plugin Submenus Specification

## Purpose

TBD: Define device-specific measurement submenu behavior and plugin-owned session routing.

## Requirements

### Requirement: Build a device-specific measurement submenu

MainApplication SHALL build the active measurement submenu from the selected assigned measurement plugin's declared action set after the selected port and route are ready. The submenu SHALL be removed or become unavailable when the assignment, route, plugin readiness, or active session is no longer valid.

#### Scenario: Elta3 is selected
- **WHEN** a ready assigned Elta3 device is selected in the Measurement menu
- **THEN** MainApplication displays a submenu containing exactly `Measure`, `Up`, `Down`, and `Enter` in that order

#### Scenario: Fennel1 is selected
- **WHEN** a ready assigned Fennel1 device is selected in the Measurement menu
- **THEN** MainApplication displays a submenu containing exactly `Measure` and does not display directional or enter actions

#### Scenario: Device is unavailable
- **WHEN** the selected measurement assignment has no ready plugin or UART route
- **THEN** MainApplication does not expose the plugin submenu as an actionable menu and preserves the existing unavailable-device behavior

### Requirement: Execute submenu actions through the selected plugin

The active measurement submenu SHALL execute `Measure` through the selected plugin's current measurement API and SHALL execute optional `Up`, `Down`, and `Enter` actions through the same active port binding. The host SHALL not create a second UART claim for submenu actions.

#### Scenario: Measure is selected
- **WHEN** the user activates `Measure` in an active measurement plugin submenu
- **THEN** MainApplication invokes the selected plugin, converts the structured result into the existing measurement record flow, and leaves save/discard behavior unchanged

#### Scenario: Elta3 directional action is selected
- **WHEN** the user activates `Up`, `Down`, or `Enter` in the Elta3 submenu
- **THEN** MainApplication routes the corresponding logical input to the selected Elta3 plugin instance and reports failures without changing the selected device

#### Scenario: Unsupported action is requested
- **WHEN** an action is not declared by the active plugin
- **THEN** MainApplication does not invoke an unrelated device adapter and returns to the active submenu with a diagnosable unsupported-action result

### Requirement: Maintain a plugin-owned submenu state machine

Each active plugin submenu SHALL have an explicit state machine that owns submenu selection, action dispatch, plugin navigation state, and teardown. MainApplication SHALL retain ownership of global system keys, back/cancel behavior, raw keyboard framing, and the lifetime of the active port binding.

#### Scenario: Plugin submenu becomes active
- **WHEN** the user selects a ready measurement device
- **THEN** MainApplication creates one active plugin submenu session, resets its action selection to the first declared action, and renders the host-owned header and footer around the plugin submenu

#### Scenario: Plugin submenu receives a navigation key
- **WHEN** an up/down/enter or equivalent logical navigation event arrives while the plugin submenu is active
- **THEN** the active plugin state machine processes the event against its declared actions and updates or dispatches the submenu without routing it to the main measurement menu

#### Scenario: Plugin submenu is exited
- **WHEN** the user presses the host back/cancel key or the assignment becomes invalid
- **THEN** MainApplication tears down the plugin session, releases its activation resources, and returns to the appropriate host measurement menu without leaving stale plugin state

### Requirement: Route keyboard input to the active plugin session

Logical keyboard events received while a plugin submenu is active SHALL be offered to that plugin's state machine before ordinary measurement-menu handling. Global system keys and host session-termination keys SHALL retain precedence, and no raw keyboard protocol bytes SHALL be exposed to the plugin.

#### Scenario: Plugin consumes a key
- **WHEN** a logical key maps to an action supported by the active plugin state machine
- **THEN** the plugin handles the key and MainApplication does not also apply the key to the parent measurement menu

#### Scenario: Global key arrives during plugin interaction
- **WHEN** a host-reserved global key or back/cancel key arrives while a plugin submenu is active
- **THEN** MainApplication handles the key according to host policy and the plugin does not receive the raw or logical event as an ordinary submenu action

#### Scenario: Plugin does not consume a key
- **WHEN** the active plugin state machine rejects a non-global logical key
- **THEN** MainApplication applies the documented fallback navigation policy without corrupting the active session or dispatching the key to another device

### Requirement: Preserve compatibility for non-plugin measurement devices

The new plugin submenu state machine SHALL not change the existing measurement workflow for built-in non-plugin devices, including fixed measurement actions, measurement result persistence, and port assignment behavior.

#### Scenario: Built-in non-plugin device is selected
- **WHEN** a ready legacy measurement device is selected
- **THEN** MainApplication continues to use its existing host action workflow and does not create a plugin submenu session
