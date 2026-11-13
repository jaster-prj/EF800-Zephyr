## ADDED Requirements

### Requirement: Discover and assign plugins through generic metadata
The virtual EF800 SHALL expose compatible discovered plugins in port-assignment
configuration using plugin-declared names, hardware capabilities, serial
defaults, and readiness state. EF800Application SHALL NOT require plugin-specific
IDs, names, protocol defaults, or device-builder branches to present or commit
an assignment.

#### Scenario: Compatible plugin appears for a port
- **WHEN** a discovered measurement plugin is ready and its required hardware
  capability is supported by a selected port
- **THEN** the port-assignment menu displays the plugin's declared name as an
  assignable option

#### Scenario: Plugin assignment configures the port
- **WHEN** the user assigns a discovered plugin to a compatible port
- **THEN** EF800Application obtains serial configuration from the plugin metadata
  or generic plugin-host contract, initializes the port transport, persists the
  plugin assignment, and reports the assignment ready only after all steps
  succeed

#### Scenario: Incompatible or unavailable plugin is excluded
- **WHEN** a plugin is missing, invalid, incompatible, unavailable, or requires
  hardware unsupported by the selected port
- **THEN** the configuration menu does not offer it as a valid assignment and
  does not create a measurement entry for it

### Requirement: Present assigned measurement plugins as the measurement menu
The virtual EF800 Measurement menu SHALL contain one entry per ready assigned
measurement plugin, using each plugin's declared name. Menu entries SHALL NOT
be represented primarily as port numbers; EF800Application SHALL resolve the
selected plugin to its assigned port and initialized transport internally.

#### Scenario: User opens the Measurement menu
- **WHEN** one or more ready measurement plugins are assigned to compatible
  ports
- **THEN** the menu lists the assigned plugin names, such as a package's
  declared instrument name, without exposing port-number entries as the menu's
  user-facing identity

#### Scenario: User selects an assigned plugin
- **WHEN** the user selects a plugin entry from the Measurement menu
- **THEN** EF800Application resolves that plugin's assignment and opens a session
  using the associated initialized transport

#### Scenario: No ready plugin is assigned
- **WHEN** no discovered measurement plugin has a valid assignment and ready
  transport
- **THEN** EF800Application reports that no measurement device is ready and does
  not enter a plugin submenu

### Requirement: Render and execute plugin-owned measurement submenus
The selected measurement plugin SHALL provide its submenu title, action labels,
available action bitmap, and plugin-owned display model through the generic
plugin contract. EF800Application SHALL render and route those actions without
hardcoded knowledge of a specific instrument or plugin ID.

#### Scenario: Plugin declares a measurement submenu
- **WHEN** a selected plugin session provides a valid display model and action
  bitmap
- **THEN** EF800Application renders the plugin-owned submenu and exposes only
  the actions declared by that plugin

#### Scenario: User executes the plugin's measure action
- **WHEN** the user selects the plugin-owned Measure action
- **THEN** EF800Application invokes the generic plugin session, displays the
  returned measurement data, and makes it available to the workspace save flow

#### Scenario: Measurement is saved to the workspace
- **WHEN** the user completes the measurement editor and saves a successful
  plugin measurement
- **THEN** EF800Application appends one measurement entry to the selected
  workspace using the plugin result and does not require plugin-specific
  persistence logic
