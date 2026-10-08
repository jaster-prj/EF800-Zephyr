## MODIFIED Requirements

### Requirement: Support interactive and automated operation
The virtual EF800 SHALL support an interactive mode synchronized closely enough to wall-clock time for human input and a deterministic scriptable mode that can inject input, observe display state, control virtual peripherals, manage native-sim plugin packages, and assert application outcomes.

#### Scenario: Developer operates the front panel
- **WHEN** the virtual EF800 is started for manual use
- **THEN** display updates, debounce, key holds, plugin deadlines, package status, virtual-device state, and periodic status behavior progress at a human-usable real-time rate

#### Scenario: Automated scenario drives the device
- **WHEN** an automated client submits a declared sequence of key, package, restart, and peripheral events
- **THEN** it can observe deterministic acknowledgements and resulting display, plugin registry, device, or application state without GUI interaction

#### Scenario: Browser uploads a package
- **WHEN** the browser uploads a valid native-sim plugin package
- **THEN** the gateway reports installation and restart-required state without changing the currently running plugin registry
