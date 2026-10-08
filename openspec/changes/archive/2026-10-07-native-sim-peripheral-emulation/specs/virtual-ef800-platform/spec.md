## MODIFIED Requirements

### Requirement: Run EF800 behavior as a native simulator application
The system SHALL provide a buildable and runnable Zephyr `native_sim` application that executes the production `ef800-main` composition root and its MainApplication domain, menu, plugin, synchronization, and thread-supervision behavior while substituting required physical EF800 devices with Zephyr-compatible emulated or simulated peripherals.

#### Scenario: Start the virtual EF800
- **WHEN** a developer builds and starts the virtual EF800 target
- **THEN** the production application initializes without an STM32 MCU or physical EF800 and exposes its virtual front-panel interface

#### Scenario: Run concurrent plugin behavior
- **WHEN** MainApplication starts a plugin operation on a worker thread
- **THEN** the virtual target uses Zephyr scheduling and synchronization primitives and keeps the MainApplication UI path independently schedulable

### Requirement: Present a live virtual EF800 LCD
The virtual EF800 SHALL maintain the observable state of a 240 x 64 monochrome EF800 display and SHALL publish complete, consistently framed display updates produced through the production T6963 driver operating against the emulated GPIO bus and virtual T6963 device.

#### Scenario: MainApplication changes the display
- **WHEN** MainApplication writes or clears EF800 display content through the production display path
- **THEN** the emulated T6963 wiring model renders the resulting 240 x 64 state and publishes it to the virtual front panel without physical GPIO hardware

#### Scenario: Client receives a partial display message
- **WHEN** a host client receives an incomplete or malformed display update
- **THEN** the client rejects that update and retains the last complete display state

### Requirement: Provide deterministic virtual peripheral behavior
The virtual EF800 SHALL provide the minimum storage, time, battery, serial-peer, display, keyboard, and device-state behaviors required by enabled MainApplication workflows through Zephyr-compatible emulated or simulated peripherals, and SHALL permit tests to configure normal responses, delays, unavailable devices, and bounded failures without modifying production workflow logic.

#### Scenario: Test configures a virtual serial instrument
- **WHEN** an automated scenario assigns a supported response script to a supported emulated EF800 port
- **THEN** the production port and plugin behavior exchanges bytes with that peer and observes the configured timing and response

#### Scenario: Test injects a peripheral failure
- **WHEN** a scenario marks a virtual peripheral unavailable or configures a bounded operation failure
- **THEN** MainApplication follows its normal degraded or recovery behavior and the failure is observable by the test
