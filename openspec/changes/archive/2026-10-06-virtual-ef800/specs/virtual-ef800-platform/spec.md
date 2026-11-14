## ADDED Requirements

### Requirement: Run EF800 behavior as a native simulator application
The system SHALL provide a buildable and runnable Zephyr `native_sim` application that executes the production EF800 Application domain, menu, plugin, synchronization, and thread-supervision behavior while substituting virtual implementations for required physical EF800 devices.

#### Scenario: Start the virtual EF800
- **WHEN** a developer builds and starts the virtual EF800 target
- **THEN** the application initializes without an STM32 MCU or physical EF800 and exposes its virtual front-panel interface

#### Scenario: Run concurrent plugin behavior
- **WHEN** EF800 Application starts a plugin operation on a worker thread
- **THEN** the virtual target uses Zephyr scheduling and synchronization primitives and keeps the EF800 Application UI path independently schedulable

### Requirement: Present a live virtual EF800 LCD
The virtual EF800 SHALL maintain the observable state of a 240 x 64 monochrome EF800 display and SHALL publish complete, consistently framed display updates to the host front panel and automated clients.

#### Scenario: EF800 Application changes the display
- **WHEN** EF800 Application writes or clears EF800 display content
- **THEN** the virtual front panel renders the resulting 240 x 64 state without requiring physical GPIO or a T6963 controller

#### Scenario: Client receives a partial display message
- **WHEN** a host client receives an incomplete or malformed display update
- **THEN** the client rejects that update and retains the last complete display state

### Requirement: Accept virtual keypad press and release input
The virtual EF800 SHALL expose the EF800 semantic key set through distinct press and release operations and SHALL deliver accepted input through the normal EF800 Application keyboard translation, parser, and application-handling path rather than directly mutating menu or plugin state.

#### Scenario: User clicks a virtual key
- **WHEN** the user presses and releases a key on the virtual front panel
- **THEN** EF800 Application receives the corresponding normal make and break behavior and updates the application through its existing keyboard handlers

#### Scenario: User holds a virtual key
- **WHEN** the front panel sends a press without an immediate release
- **THEN** the key remains logically held until release or release-all and any supported repeat behavior follows the virtual keyboard policy

#### Scenario: Front panel disconnects with held keys
- **WHEN** the active front-panel connection closes while one or more virtual keys are held
- **THEN** the virtual keyboard releases all held keys so stale input does not remain active

### Requirement: Support interactive and automated operation
The virtual EF800 SHALL support an interactive mode synchronized closely enough to wall-clock time for human input and a deterministic scriptable mode that can inject input, observe display state, control virtual peripherals, and assert application outcomes.

#### Scenario: Developer operates the front panel
- **WHEN** the virtual EF800 is started for manual use
- **THEN** display updates, debounce, key holds, plugin deadlines, and periodic status behavior progress at a human-usable real-time rate

#### Scenario: Automated scenario drives the device
- **WHEN** an automated client submits a declared sequence of key and peripheral events
- **THEN** it can observe deterministic acknowledgements and resulting display or application state without GUI interaction

### Requirement: Connect the container simulation to a host front panel
The virtual EF800 SHALL use a documented bidirectional transport that can connect a simulator process in the reusable development container to a front-panel process on the Windows host without requiring graphical display forwarding into the container.

#### Scenario: Front panel connects after simulator startup
- **WHEN** a compatible host front panel connects to the simulator endpoint
- **THEN** it receives the current display state and can submit supported keypad operations

#### Scenario: No front panel is connected
- **WHEN** the simulator runs headlessly for an automated test
- **THEN** EF800 Application continues to run and the test interface remains usable without a desktop window

### Requirement: Provide deterministic virtual peripheral behavior
The virtual EF800 SHALL provide the minimum storage, time, battery, serial-peer, and device-state behaviors required by enabled EF800 Application workflows, and SHALL permit tests to configure normal responses, delays, unavailable devices, and bounded failures without modifying production workflow logic.

#### Scenario: Test configures a virtual serial instrument
- **WHEN** an automated scenario assigns a supported response script to a virtual EF800 port
- **THEN** the production port and plugin behavior exchanges bytes with that peer and observes the configured timing and response

#### Scenario: Test injects a peripheral failure
- **WHEN** a scenario marks a virtual peripheral unavailable or configures a bounded operation failure
- **THEN** EF800 Application follows its normal degraded or recovery behavior and the failure is observable by the test

### Requirement: Exercise plugin supervision behavior without hardware
The virtual EF800 SHALL permit behavioral tests to run healthy, delayed, non-returning, stale-completion, malformed-request, and host-service-failure plugin scenarios while observing UI responsiveness, deadlines, cleanup, quarantine, navigation, and workspace integrity.

#### Scenario: Plugin operation exceeds its deadline
- **WHEN** a virtual test plugin does not complete before its host-defined deadline
- **THEN** the production supervisor behavior handles the timeout, discards unaccepted output, performs configured cleanup, and reports the resulting quarantine or escalation state

#### Scenario: UI input arrives during plugin work
- **WHEN** a plugin worker is active and the test submits a supported keypad event
- **THEN** the EF800 Application UI path remains schedulable and processes or deliberately defers the input according to production policy

#### Scenario: Healthy plugin follows a failed plugin
- **WHEN** one virtual plugin is quarantined after safe cleanup and another valid plugin is selected
- **THEN** the unrelated plugin remains operable if shared virtual resources are in a known-safe state

### Requirement: Preserve the hardware qualification boundary
Virtual EF800 results SHALL be identified as behavioral simulation evidence and SHALL NOT be accepted as proof of STM32 register behavior, ARM executable loading, MPU permissions, CPU exception classification, bootloader handoff, electrical timing, signal integrity, physical QSPI or USB operation, or fitted peripheral wiring.

#### Scenario: Virtual behavioral suite passes
- **WHEN** all virtual EF800 scenarios pass
- **THEN** the project may claim the covered EF800 Application and plugin behaviors passed without hardware but retains every applicable hardware-specific release gate

#### Scenario: MPU containment requires qualification
- **WHEN** a requirement depends on preventing an unauthorized memory access or classifying an ARM fault
- **THEN** the result requires target evidence and cannot be closed by substituting a virtual plugin failure event
