## MODIFIED Requirements

### Requirement: Measurement packages own submenu definitions

Each valid measurement `.pmp` package SHALL declare its complete measurement workflow and submenu metadata through the EF800 plugin contract. EF800 Application SHALL render the selected plugin's bounded display model rather than constructing device-specific or common measurement action tables. The package SHALL own code, point number, staff height, Save, Discard, and Fixmessung behavior.

#### Scenario: Elta3 declares directional actions
- **WHEN** the Elta3 package is loaded and assigned to a compatible ready port
- **THEN** its display exposes measurement, up, down, enter, and its complete pending-result workflow according to the package declaration

#### Scenario: Fennel1 declares measurement only
- **WHEN** the Fennel1 package is loaded and assigned to a compatible ready port
- **THEN** its display exposes measurement without unsupported directional actions and owns the complete pending-result workflow

### Requirement: Measurement packages handle logical keyboard input

Measurement packages SHALL receive logical keyboard events for their complete workflow through the plugin contract and SHALL return explicit consumed, unsupported, navigation, or error results. Packages SHALL not receive raw keyboard UART framing or access the keyboard device directly. EF800 Application SHALL only forward generic input and host-service outcomes.

#### Scenario: Plugin consumes a workflow action
- **WHEN** EF800 Application forwards a logical action key to an active Elta3 or Fennel1 session
- **THEN** the selected package handles the action and returns a result that tells EF800 Application whether the event was consumed

#### Scenario: Host global key has precedence
- **WHEN** a global system key or host back/cancel key arrives during a plugin session
- **THEN** EF800 Application handles the key without forwarding it as a package-local action

#### Scenario: Plugin requests workspace append
- **WHEN** the plugin completes Save or Fixmessung for a valid pending result
- **THEN** the plugin returns a bounded serialized workspace line and EF800 Application performs only the generic workspace append operation

#### Scenario: Unsupported Fennel1 direction is selected
- **WHEN** a directional logical action is sent to a Fennel1 package that does not declare that action
- **THEN** the package returns an explicit unsupported result and does not emit a device command

### Requirement: Measurement packages provide bounded display models

Measurement packages SHALL provide bounded display metadata and content for their complete measurement workflow through the EF800 plugin contract. EF800 Application SHALL render the returned model and SHALL reject or report oversized content without truncation.

#### Scenario: Plugin renders its measurement workflow
- **WHEN** an active package is asked to render its current state
- **THEN** it returns a bounded title, action selection, metadata/result rows, and optional footer hints for the host renderer

#### Scenario: Measurement result updates plugin display state
- **WHEN** a package completes a measurement
- **THEN** the package updates its display model to expose staff-height, Save, Discard, and Fixmessung behavior without a host-owned replacement menu

#### Scenario: Display content exceeds the contract limit
- **WHEN** a package returns a title, row, or screen model exceeding the declared bounds
- **THEN** EF800 Application rejects the model with a diagnosable error and does not silently truncate or render out-of-bounds content

### Requirement: EF800 Application remains the host resource owner

EF800 Application SHALL retain ownership of raw keyboard decoding, LCD hardware access, global navigation, plugin lifecycle, physical port assignment, UART configuration, port service lifetime, workspace loading, current point/code context, and opaque workspace-line append. Measurement packages SHALL use only declared host services and SHALL not access Zephyr, T6963, GPIO, DeviceTree, raw UART types, or filesystem/workspace objects.

#### Scenario: Package session is opened
- **WHEN** a valid assigned package is activated
- **THEN** EF800 Application creates the package session, supplies current workspace point/code context and host-controlled services, and keeps ownership of all resource lifetimes

#### Scenario: Package returns a workspace record
- **WHEN** a valid package returns a bounded serialized measurement line
- **THEN** EF800 Application appends the opaque line through the workspace service and does not parse measurement-specific fields

#### Scenario: Package session is closed
- **WHEN** the user leaves the package submenu or the assignment becomes unavailable
- **THEN** EF800 Application stops package calls, destroys the package instance, and releases the port after package teardown

### Requirement: Elta3 and Fennel1 are independently packaged

The build SHALL produce independently installable `.pmp` packages from `application/plugins/measurement-plugin/elta3` and `application/plugins/measurement-plugin/fennel1`. Each package SHALL contain its own descriptor, complete measurement behavior, submenu/input behavior, display-model behavior, serialized workspace-record behavior, and stable plugin identity. The common workflow SHALL not be duplicated inside EF800 Application.

#### Scenario: Both package targets are built
- **WHEN** the measurement-plugin build is configured for package generation
- **THEN** it produces separately verifiable Elta3 and Fennel1 `.pmp` artifacts without compiling either implementation into EF800 Application

#### Scenario: One package is unavailable
- **WHEN** only one of the two `.pmp` files is present or valid at startup
- **THEN** the available package remains discoverable and the missing package is reported unavailable without blocking unrelated host functionality

### Requirement: Package contract compatibility is explicit

The host SHALL validate the UI, logical-input, submenu, serialized-output, and display-model ABI version and structure sizes before activating a measurement package. Packages that do not support the required contract SHALL be rejected or marked unavailable.

#### Scenario: Package supports the required UI contract
- **WHEN** a package descriptor advertises a compatible contract version and valid structure sizes
- **THEN** EF800 Application accepts the package for discovery and activation subject to normal assignment and port checks

#### Scenario: Package lacks the required contract
- **WHEN** a package advertises an older or incompatible measurement UI contract
- **THEN** EF800 Application rejects it before execution and records a diagnosable incompatibility state
