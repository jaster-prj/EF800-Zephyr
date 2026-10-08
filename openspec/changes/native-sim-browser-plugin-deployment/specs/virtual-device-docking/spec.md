## ADDED Requirements

### Requirement: Configure bounded virtual instrument profiles
The browser SHALL expose bounded virtual instrument profiles that define a simulated port, availability, serial configuration, response delay, bounded error behavior, and request/response script without exposing arbitrary filesystem or simulator internals.

#### Scenario: Dock a virtual instrument
- **WHEN** the user assigns a supported virtual instrument profile to a native-sim port
- **THEN** the simulator applies the profile and reports the port as docked with its configured availability and serial behavior

#### Scenario: Configure a scripted response
- **WHEN** the user supplies a bounded request/response script for a docked instrument
- **THEN** the production port service receives the scripted response and configured delay when a plugin transmits the matching request

### Requirement: Exercise plugin behavior against docked devices
The native-sim platform SHALL allow a discovered native-sim plugin to be assigned to a compatible virtual port and SHALL route plugin port-service operations to the docked virtual instrument.

#### Scenario: Measurement plugin exchanges data
- **WHEN** a valid native-sim Measurement plugin is assigned to a ready port with a docked virtual instrument
- **THEN** the plugin can send and receive through the host-owned Port service and produce its bounded measurement result

#### Scenario: Docked instrument is unavailable
- **WHEN** a virtual instrument profile is marked unavailable or configured with a bounded error
- **THEN** the plugin observes the normal host-service failure and the simulator exposes the resulting diagnostic state

### Requirement: Preserve deterministic virtual-device state
The simulator SHALL expose acknowledgements and current virtual-device state to the browser and SHALL apply profile changes deterministically without altering physical firmware behavior.

#### Scenario: Browser reconnects after profile configuration
- **WHEN** the browser reconnects to a running simulator after configuring a docked instrument
- **THEN** it receives the current bounded profile state or an explicit unavailable state
