## ADDED Requirements

### Requirement: Provide a healthy native-sim peripheral profile
The native-sim EF800 SHALL provide a default profile in which the GPIO banks, UART devices, I2C devices, simulated storage, RTC, battery gauge, keyboard controller, and LCD device are present and operational so the production EF800Application startup path can complete without physical hardware.

#### Scenario: Production application boots with healthy peripherals
- **WHEN** the native-sim EF800 is started without fault configuration
- **THEN** the production application observes ready required devices, mounts disposable storage, completes keyboard startup, initializes the LCD path, and remains running

### Requirement: Preserve production device API paths
The native-sim target SHALL expose devicetree devices and emulation implementations through the Zephyr GPIO, UART, I2C, flash, filesystem, RTC, and related device APIs used by the production application, without requiring a simulator-only replacement of application workflow logic.

#### Scenario: Existing production driver reaches a virtual device
- **WHEN** the production application configures a GPIO, transmits on a UART, accesses an I2C peripheral, or mounts storage
- **THEN** the request is handled through the corresponding Zephyr device API and the configured native-sim emulator or simulated device

### Requirement: Model connected peripheral wiring
The native-sim target SHALL propagate observable output state between emulated devices according to the EF800 wiring, including MCP23017-controlled keyboard reset and LCD control, T6963 parallel-bus transactions, keyboard UART traffic, and scripted measurement-port peers.

#### Scenario: LCD driver writes a display byte
- **WHEN** the production T6963 driver toggles emulated data and control GPIOs for a write cycle
- **THEN** the virtual T6963 model consumes the cycle and updates the published 240 x 64 framebuffer

#### Scenario: Keyboard reset is released
- **WHEN** the production application changes the emulated MCP23017 keyboard-reset output to the released state
- **THEN** the virtual keyboard controller emits its configured startup response through the emulated keyboard UART

### Requirement: Configure deterministic peripheral profiles
The native-sim target SHALL support explicit profile controls for peripheral availability, response delays, register values, scripted serial responses, storage state, and bounded operation failures while leaving the healthy default behavior unchanged.

#### Scenario: Test disables a peripheral
- **WHEN** a scenario marks an emulated RTC, battery gauge, storage device, serial peer, or display device unavailable
- **THEN** the production application receives the corresponding bounded device error and follows its normal degraded or recovery behavior

### Requirement: Configure profiles through a bounded headless protocol
The native-sim target SHALL accept validated profile-control messages through the internal simulator protocol and SHALL return bounded acknowledgements or errors; browser front-panel clients SHALL not receive unrestricted profile-control authority.

#### Scenario: Headless client changes a serial profile
- **WHEN** a direct scenario client sends a valid bounded profile-control message for a serial peer
- **THEN** the native simulator applies the response, delay, availability, or error configuration and acknowledges the operation

#### Scenario: Invalid profile control is rejected
- **WHEN** a client sends an unknown device, unsupported operation, oversized payload, or out-of-range value
- **THEN** the simulator rejects the message without changing profile state

### Requirement: Isolate virtual storage state
The native-sim target SHALL use disposable storage by default and SHALL require an explicit configuration to reuse persistent simulator storage; it SHALL never select a physical EF800 storage image implicitly.

#### Scenario: Two default runs start with isolated storage
- **WHEN** two native-sim runs use the default storage profile
- **THEN** each run receives an isolated disposable filesystem and no state is read from a physical EF800 image
