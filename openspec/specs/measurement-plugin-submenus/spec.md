# Measurement Plugin Submenus Specification

## Purpose
Define plugin-owned measurement submenu behavior and session routing.

## Requirements
### Requirement: Build a device-specific measurement submenu

MainApplication SHALL render the active measurement plugin's bounded display model and route generic logical input to the active plugin session. The plugin SHALL own its complete measurement workflow, including instrument actions, point metadata, pending-result actions, and teardown of its own logical measurement state. MainApplication SHALL not construct measurement-specific action labels or action sequencing.

#### Scenario: Elta3 is selected
- **WHEN** a ready assigned Elta3 device is selected in the Measurement menu
- **THEN** MainApplication opens a generic Elta3 plugin session whose display exposes `Measure`, `Up`, `Down`, `Enter`, and the plugin-owned measurement workflow

#### Scenario: Fennel1 is selected
- **WHEN** a ready assigned Fennel1 device is selected in the Measurement menu
- **THEN** MainApplication opens a generic Fennel1 plugin session whose display exposes measurement without unsupported directional actions and includes the plugin-owned result workflow

#### Scenario: Measurement result is pending
- **WHEN** either plugin completes a measurement successfully
- **THEN** the plugin renders its own staff-height, save, discard, and fixed-measurement actions without MainApplication replacing the display with a host action menu

#### Scenario: Device is unavailable
- **WHEN** the selected measurement assignment has no ready plugin or UART route
- **THEN** MainApplication does not expose the plugin session as actionable and preserves the existing unavailable-device behavior

### Requirement: Execute submenu actions through the selected plugin

The active plugin session SHALL execute measurement and optional instrument actions through the same active port binding. MainApplication SHALL forward generic input and host-service results only; it SHALL not dispatch measurement-specific actions or create a second UART claim.

#### Scenario: Measure is selected
- **WHEN** the user activates `Measure` in an active measurement plugin session
- **THEN** the selected plugin collects its required code and point number, invokes its instrument protocol, and renders its pending-result workflow

#### Scenario: Elta3 directional action is selected
- **WHEN** the user activates `Up`, `Down`, or `Enter` in the Elta3 session
- **THEN** MainApplication forwards the generic logical input through the selected Elta3 plugin instance and reports the plugin result without changing the selected device

#### Scenario: Plugin saves a measurement
- **WHEN** the plugin activates Save for a valid pending result
- **THEN** the plugin returns a bounded complete workspace line and MainApplication appends that opaque line to the selected `workspace.dat`

#### Scenario: Unsupported action is requested
- **WHEN** an action is not declared or supported by the active plugin
- **THEN** the plugin returns an explicit unsupported result and MainApplication does not invoke an unrelated device adapter

### Requirement: Maintain a plugin-owned submenu state machine

Each active measurement plugin SHALL own its measurement state machine, including action selection, point code, point number, staff height, pending result, Save, Discard, Fixmessung, and plugin-local teardown. MainApplication SHALL retain ownership of global system keys, raw keyboard framing, the active port binding, workspace context transfer, and opaque workspace append.

#### Scenario: Plugin session becomes active
- **WHEN** the user selects a ready measurement device
- **THEN** MainApplication creates one plugin session and supplies the workspace's current point number and code as generic context

#### Scenario: Fixed measurement advances the point
- **WHEN** the user selects Fixmessung for a pending measurement
- **THEN** the plugin returns the current serialized record for append, uses the updated workspace context to advance the point number, triggers the next measurement, and remains in its plugin-owned workflow

#### Scenario: Plugin session is exited
- **WHEN** the user presses the host back/cancel key or the assignment becomes invalid
- **THEN** MainApplication tears down the plugin session and releases its activation resources without leaving a stale session

### Requirement: Route keyboard input to the active plugin session

Logical keyboard events received while a plugin session is active SHALL be offered to that plugin before ordinary parent-menu handling. Global system keys and host session-termination keys SHALL retain precedence. No raw keyboard protocol bytes or measurement-specific action dispatch SHALL be exposed to MainApplication.

#### Scenario: Plugin consumes a key
- **WHEN** a logical key maps to an action supported by the active plugin workflow
- **THEN** the plugin handles the key and MainApplication does not apply it to the parent measurement menu

#### Scenario: Global key arrives during plugin interaction
- **WHEN** a host-reserved global key or back/cancel key arrives while a plugin session is active
- **THEN** MainApplication handles the key according to host policy and the plugin does not receive it as an ordinary action

#### Scenario: Plugin does not consume a key
- **WHEN** the active plugin rejects a non-global logical key
- **THEN** MainApplication applies the documented fallback navigation policy without dispatching the key to another device

### Requirement: Preserve compatibility for non-plugin measurement devices

The plugin session architecture SHALL not change the existing measurement workflow for built-in non-plugin devices, including fixed measurement actions, measurement result persistence, and port assignment behavior.

#### Scenario: Built-in non-plugin device is selected
- **WHEN** a ready legacy measurement device is selected
- **THEN** MainApplication continues to use its existing host action workflow and does not create a plugin measurement session


