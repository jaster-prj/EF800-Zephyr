# ef800-plugin-integration Specification

## Purpose

TBD: Define the EF800 plugin discovery, registry, lifecycle, bounded services, and storage ownership behavior.

## Requirements

### Requirement: Discover plugins at EF800 Application startup
EF800 Application SHALL scan and validate configured plugin packages only during startup when internal file storage is reachable. If storage is unavailable, EF800 Application SHALL continue without external plugins and SHALL not format, overwrite, or monitor the unavailable storage.

#### Scenario: Storage is reachable during startup
- **WHEN** EF800 Application initializes and internal file storage is mounted and available
- **THEN** it scans the configured plugin location, validates candidate packages, and registers valid plugins before exposing plugin-dependent menus and assignments

#### Scenario: Storage is unavailable during startup
- **WHEN** EF800 Application initializes without reachable internal file storage
- **THEN** built-in functionality remains available, external plugins are unavailable for that boot, and no plugin filesystem access is attempted

#### Scenario: Plugin package is invalid
- **WHEN** startup scanning finds a corrupt, incompatible, duplicate, or otherwise invalid candidate package
- **THEN** the package is rejected before execution and the failure is diagnosable without preventing unrelated valid plugins from being registered

### Requirement: Use one EF800 plugin registry and lifecycle
EF800 Application SHALL expose built-in and externally loaded Measurement and Calculation plugins through one EF800-owned registry and dispatch path. Plugin instances SHALL be created only after interface and service validation and SHALL be destroyed before their claimed resources are released.

#### Scenario: Valid plugin is activated and closed
- **WHEN** the host selects a registered plugin and its required services are available
- **THEN** EF800 Application creates the instance, invokes it through the declared interface, and destroys it before returning to the host menu

#### Scenario: Plugin creation fails
- **WHEN** a registered plugin cannot be created or its required service contract is unavailable
- **THEN** EF800 Application reports the plugin as unavailable and releases any resources already acquired for that activation

### Requirement: Extend the Calculation menu with installed calculation plugins
EF800 Application SHALL expose each valid installed Calculation plugin in the Calculation menu without requiring a physical port assignment. Calculation plugins SHALL receive bounded workspace snapshots and bounded output buffers, and SHALL not receive filesystem handles or implicit permission to mutate workspace state.

#### Scenario: Calculation plugin is available
- **WHEN** a valid Calculation plugin is registered and the user opens the Calculation menu
- **THEN** the plugin is shown as a selectable calculation action

#### Scenario: Calculation plugin produces a result
- **WHEN** the host invokes a selected Calculation plugin with a bounded workspace snapshot
- **THEN** the host receives an explicitly sized result and decides whether to preview, save, append, or reject it

#### Scenario: Calculation input or output exceeds its bound
- **WHEN** a workspace snapshot or plugin result does not fit its configured buffer
- **THEN** the operation fails explicitly and no truncated data is displayed or persisted

### Requirement: Expose Measurement plugins only through valid port bindings
EF800 Application SHALL expose a Measurement plugin as operationally ready only when its package is valid, its persisted assignment exists, the assigned physical port and connection satisfy the plugin requirements, and the host-owned UART route is ready. EF800 Application SHALL preserve and diagnose assignments whose package or route is missing rather than silently remapping them.

#### Scenario: Assigned measurement plugin is ready
- **WHEN** a valid Measurement plugin is assigned to a compatible port with a ready UART route
- **THEN** the Measurement menu exposes the assigned binding as selectable and the host passes a Port service to the plugin

#### Scenario: Measurement package is missing
- **WHEN** persistence references a Measurement plugin whose package is not available at startup
- **THEN** the assignment remains preserved and is reported as missing rather than mapped to a built-in or different plugin

#### Scenario: Measurement route is unavailable
- **WHEN** a valid assigned Measurement plugin has no ready compatible UART route
- **THEN** the assignment is reported as unavailable and cannot be activated

#### Scenario: Measurement directional action is supported
- **WHEN** a selected Measurement plugin declares directional or enter actions
- **THEN** those actions use the same host-owned port binding as measurement and do not claim a second independent UART

### Requirement: Keep storage ownership and USB MSD behavior unchanged
EF800 Application SHALL not scan, execute, or modify plugin files while EF800 Backup owns internal storage through USB MSD. Plugin discovery SHALL occur again after the existing application restart into a storage-reachable EF800 Application mode; hot replacement and filesystem monitoring are not required.

#### Scenario: EF800 Backup owns storage
- **WHEN** the device is operating in USB MSD mode
- **THEN** EF800 Application does not access plugin files or execute plugin code

#### Scenario: Application restarts after USB mode change
- **WHEN** the existing USB transition restarts the application into CDC or none mode with reachable storage
- **THEN** EF800 Application performs normal startup discovery and uses the resulting registry for that boot

### Requirement: Provide host-controlled plugin UI and logical keyboard services
Plugins SHALL be able to provide bounded menu metadata and plugin display content and SHALL receive logical keyboard events while their screen is active. EF800 Application SHALL own the LCD header, rendering boundary, navigation/session lifetime, raw keyboard protocol, and global system keys. Plugins SHALL not access the T6963 driver, GPIO descriptors, DeviceTree objects, keyboard UART framing, or create UI threads.

#### Scenario: Plugin contributes a menu entry
- **WHEN** a valid plugin declares menu metadata
- **THEN** EF800 Application places the contribution in the appropriate host-owned category without allowing the plugin to replace the header or arbitrary host menu state

#### Scenario: Plugin screen is displayed
- **WHEN** the host activates a plugin UI session
- **THEN** EF800 Application renders the plugin's bounded screen model below the host-owned header and applies host-owned footer and navigation policy

#### Scenario: Plugin handles keyboard input
- **WHEN** a logical key event arrives while a plugin session is active
- **THEN** EF800 Application forwards the event to the plugin, honors its consumed/navigation result within the session, and retains ownership of global system keys and session termination

#### Scenario: Plugin requests text input
- **WHEN** a plugin enters an explicit bounded text-input mode
- **THEN** it receives logical character, editing, confirm, and cancel events through the keyboard service without receiving raw UART bytes

### Requirement: Protect host resources and workspace mutation
EF800 Application SHALL serialize plugin operations with host UI and resource ownership. Plugins SHALL use host services for ports, workspace access, display, keyboard input, logging, and clock access, and workspace mutation SHALL require an explicit host operation with appropriate user confirmation.

#### Scenario: Plugin attempts direct resource access
- **WHEN** a plugin contract does not provide a permitted host service for a resource
- **THEN** the plugin has no contract-level access to that resource and the host continues to own its lifetime

#### Scenario: Plugin requests workspace mutation
- **WHEN** a plugin produces content intended to change workspace data
- **THEN** EF800 Application validates the bounded result and requires the host's explicit mutation flow before persistence
