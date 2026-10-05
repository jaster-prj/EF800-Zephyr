## ADDED Requirements

### Requirement: Measurement packages own submenu definitions
Each valid measurement `.pmp` package SHALL declare its supported measurement actions and submenu metadata through the EF800 plugin contract. MainApplication SHALL construct the active measurement submenu from the selected plugin's declaration rather than from device-specific host tables.

#### Scenario: Elta3 declares directional actions
- **WHEN** the Elta3 package is loaded and assigned to a compatible ready port
- **THEN** its menu contribution exposes measurement, up, down, and enter actions according to the package declaration

#### Scenario: Fennel1 declares measurement only
- **WHEN** the Fennel1 package is loaded and assigned to a compatible ready port
- **THEN** its menu contribution exposes measurement without unsupported directional actions

### Requirement: Measurement packages handle logical keyboard input
Measurement packages SHALL receive logical keyboard events through the plugin contract and SHALL return an explicit consumed, unsupported, navigation, or error result. Packages SHALL not receive raw keyboard UART framing or access the keyboard device directly.

#### Scenario: Plugin consumes a submenu action
- **WHEN** MainApplication forwards a logical action key to an active Elta3 or Fennel1 session
- **THEN** the selected package handles the action and returns a result that tells MainApplication whether the event was consumed

#### Scenario: Host global key has precedence
- **WHEN** a global system key or host back/cancel key arrives during a plugin session
- **THEN** MainApplication handles the key without forwarding it as a package-local action

#### Scenario: Unsupported Fennel1 direction is selected
- **WHEN** a directional logical action is sent to a Fennel1 package that does not declare that action
- **THEN** the package returns an explicit unsupported result and does not emit a device command

### Requirement: Measurement packages provide bounded display models
Measurement packages SHALL provide bounded display metadata and content through the EF800 plugin contract. MainApplication SHALL render the returned model within the host-owned display body and SHALL reject or report oversized content without truncation.

#### Scenario: Plugin renders its measurement submenu
- **WHEN** an active package is asked to render its current state
- **THEN** it returns a bounded title, action selection, measurement/status rows, and optional footer hints for the host renderer

#### Scenario: Measurement result updates plugin display state
- **WHEN** a package completes a measurement
- **THEN** the package updates its display model so the host can render the formatted result without device-specific formatting logic in MainApplication

#### Scenario: Display content exceeds the contract limit
- **WHEN** a package returns a title, row, or screen model exceeding the declared bounds
- **THEN** MainApplication rejects the model with a diagnosable error and does not silently truncate or render out-of-bounds content

### Requirement: MainApplication remains the host resource owner
MainApplication SHALL retain ownership of raw keyboard decoding, LCD hardware access, global navigation, plugin lifecycle, physical port assignment, UART configuration, and port service lifetime. Measurement packages SHALL use only declared host services and SHALL not access Zephyr, T6963, GPIO, DeviceTree, or raw UART types.

#### Scenario: Package session is opened
- **WHEN** a valid assigned package is activated
- **THEN** MainApplication creates the package session, supplies the host-controlled services, and keeps ownership of all resource lifetimes

#### Scenario: Package session is closed
- **WHEN** the user leaves the package submenu or the assignment becomes unavailable
- **THEN** MainApplication stops package calls, destroys the package instance, and releases the port after package teardown

### Requirement: Elta3 and Fennel1 are independently packaged
The build SHALL produce independently installable `.pmp` packages from `application/plugins/measurement-plugin/elta3` and `application/plugins/measurement-plugin/fennel1`. Each package SHALL contain its own descriptor, measurement behavior, submenu/input behavior, display-model behavior, and stable plugin identity.

#### Scenario: Both package targets are built
- **WHEN** the measurement-plugin build is configured for package generation
- **THEN** it produces separately verifiable Elta3 and Fennel1 `.pmp` artifacts without compiling either implementation into MainApplication

#### Scenario: One package is unavailable
- **WHEN** only one of the two `.pmp` files is present or valid at startup
- **THEN** the available package remains discoverable and the missing package is reported unavailable without blocking unrelated host functionality

### Requirement: Package contract compatibility is explicit
The host SHALL validate the UI, logical-input, submenu, and display-model ABI version and structure sizes before activating a measurement package. Packages that do not support the required contract SHALL be rejected or marked unavailable.

#### Scenario: Package supports the required UI contract
- **WHEN** a package descriptor advertises a compatible contract version and valid structure sizes
- **THEN** MainApplication accepts the package for discovery and activation subject to normal assignment and port checks

#### Scenario: Package lacks the required contract
- **WHEN** a package advertises an older or incompatible measurement UI contract
- **THEN** MainApplication rejects it before execution and records a diagnosable incompatibility state
