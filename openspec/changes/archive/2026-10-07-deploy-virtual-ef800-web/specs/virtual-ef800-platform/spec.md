## MODIFIED Requirements

### Requirement: Connect the container simulation to a host front panel
The virtual EF800 SHALL use its documented, versioned binary transport between the dedicated simulator container and one web-gateway client on the private Compose network, and the gateway SHALL expose the interactive front panel to a localhost browser without graphical display forwarding or a host Python GUI process.

#### Scenario: Front panel connects after simulator startup
- **WHEN** the compatible web gateway connects to the simulator endpoint
- **THEN** it receives the current display state and can submit supported keypad operations through the existing simulator protocol

#### Scenario: Simulator starts after the gateway
- **WHEN** the gateway starts before the simulator endpoint is available
- **THEN** it keeps serving the browser interface, reports the simulator as disconnected, and reconnects with bounded backoff

#### Scenario: Browser connects after gateway startup
- **WHEN** a browser opens the localhost front panel
- **THEN** it receives the latest complete display state and current simulator connection status without connecting directly to the simulator TCP endpoint

#### Scenario: No front panel is connected
- **WHEN** the simulator runs headlessly for an automated test
- **THEN** EF800 Application continues to run and the existing automated client interface remains usable without a browser or desktop window

## ADDED Requirements

### Requirement: Present the EF800 front panel in a browser
The web front panel SHALL render the complete 240 x 64 monochrome display, expose the supported semantic keypad, and communicate live state and input through a bounded WebSocket interface.

#### Scenario: Display frame arrives from the simulator
- **WHEN** the gateway accepts a complete valid LCD frame
- **THEN** it retains that frame and broadcasts its sequence, pixels, and available logical text state to connected browsers

#### Scenario: Browser receives a malformed message
- **WHEN** a browser receives or submits a message that does not conform to the declared browser protocol
- **THEN** the invalid message is rejected without changing the retained LCD frame or injecting keypad input

#### Scenario: User operates the browser keypad
- **WHEN** the controlling browser presses, holds, and releases a supported mouse, touch, or mapped host-keyboard control
- **THEN** the gateway sends distinct semantic key-down and key-up operations through the normal simulator keyboard path

### Requirement: Separate browser viewing from device control
The web gateway SHALL allow multiple browser sessions to observe display and connection state but SHALL grant keypad control to at most one browser session at a time.

#### Scenario: Browsers join as viewers
- **WHEN** one or more browsers connect to the front panel
- **THEN** each browser can observe current and subsequent complete display states without automatically gaining keypad control

#### Scenario: A viewer acquires control
- **WHEN** no controller is active and a viewer requests control
- **THEN** the gateway grants an explicit controller lease to that browser and reports the ownership state to all connected browsers

#### Scenario: Another viewer requests occupied control
- **WHEN** a controller lease is active and another viewer requests control
- **THEN** the gateway rejects the request without forwarding that viewer's keypad events

#### Scenario: Controller disconnects with held keys
- **WHEN** the controlling browser disconnects, releases control, or becomes invalid while one or more keys may be held
- **THEN** the gateway sends release-all before making control available to another browser

#### Scenario: Simulator connection is lost
- **WHEN** the gateway loses its simulator connection
- **THEN** it clears controller ownership and held-key tracking, marks cached display state as stale, and requires fresh control acquisition after reconnection

### Requirement: Preserve headless protocol compatibility
The simulator's versioned binary front-panel protocol SHALL remain available to the existing headless scenario client when the web gateway is not connected.

#### Scenario: Run an automated scenario without Compose web control
- **WHEN** an automated test starts the simulator and connects the compatible scenario client directly
- **THEN** the client can receive LCD state and submit semantic key operations without starting the web service

#### Scenario: Validate shared protocol behavior
- **WHEN** protocol framing or payload decoding changes
- **THEN** compatibility tests cover fragmented frames, multiple frames, invalid bounds, and the message forms consumed by both the gateway and scenario tooling
