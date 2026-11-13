# native-sim-ef800-peripherals Specification

## Purpose

Provide Zephyr-compatible emulated and simulated peripherals that let the production EF800Application run on native_sim with deterministic healthy and failure profiles.

## Requirements

### Requirement: Provide native peripherals through the shared EF800Application target
The native-sim GPIO, UART, I2C, storage, RTC, battery, LCD, keyboard, profile, and front-panel emulation SHALL remain available when `application/ef800-application` is built for `native_sim`, without requiring an `application/ef800-virtual` application wrapper.

#### Scenario: Build the shared target for native_sim
- **WHEN** a developer builds `application/ef800-application` with `-b native_sim`
- **THEN** the production source manifest links with the native peripheral adapters and starts the same EF800Application composition root

#### Scenario: Physical build remains hardware-specific
- **WHEN** a developer builds `application/ef800-application` with `-b ef800`
- **THEN** the build selects STM32, physical USB, QSPI, watchdog, and board devicetree adapters without linking native-sim emulation code

### Requirement: Remove the duplicate virtual application model
The project SHALL not require a second simulator-specific EF800Application or menu implementation after the shared target supports native_sim.

#### Scenario: Virtual application wrapper is removed
- **WHEN** the native-sim target is built and launched
- **THEN** its entry point and application workflows originate from `application/ef800-application`, while retained virtual modules are limited to peripheral, transport, and front-panel infrastructure

### Requirement: Provide a healthy native-sim peripheral profile
The native-sim EF800 SHALL provide a default profile in which the GPIO banks, UART devices, I2C devices, simulated storage, RTC, battery gauge, keyboard controller, and LCD device are present and operational so the production EF800Application startup path can complete without physical hardware.

#### Scenario: Production application boots with healthy peripherals
- **WHEN** the native-sim EF800 is started without fault configuration
- **THEN** the production application observes ready required devices, mounts disposable storage, completes keyboard startup, initializes the LCD path, and remains running

### Requirement: Preserve production device API paths
The native-sim target SHALL expose devicetree devices and emulation implementations through the Zephyr GPIO, UART, I2C, flash, filesystem, RTC, and related device APIs used by the production application.

#### Scenario: Existing production driver reaches a virtual device
- **WHEN** the production application configures a GPIO, transmits on a UART, accesses an I2C peripheral, or mounts storage
- **THEN** the request is handled through the corresponding Zephyr device API and configured native-sim device

### Requirement: Model connected peripheral wiring
The native-sim target SHALL propagate observable output state between emulated devices according to the EF800 wiring, including MCP23017-controlled keyboard reset and LCD control, T6963 parallel-bus transactions, keyboard UART traffic, and scripted measurement-port peers. The LCD wiring SHALL connect production T6963 GPIO transactions to the configurable T6963C model and its incremental packed framebuffer output.

#### Scenario: LCD driver writes a display byte
- **WHEN** the production T6963 driver toggles emulated data and control GPIOs for a write cycle
- **THEN** the virtual T6963 model consumes the cycle, invalidates the affected display region, updates the packed framebuffer when pixels change, and exposes the resulting complete snapshot

#### Scenario: LCD driver reads display data
- **WHEN** the production T6963 driver performs a status or display-data read cycle
- **THEN** the virtual T6963 model drives the configured status or RAM value onto the emulated data pins and applies the command's read-address semantics

#### Scenario: Keyboard reset is released
- **WHEN** the production application changes the emulated MCP23017 keyboard-reset output to the released state
- **THEN** the virtual keyboard controller emits its configured startup response through the emulated keyboard UART

### Requirement: Configure deterministic peripheral profiles
The native-sim target SHALL support explicit profile controls for availability, response delays, register values, scripted serial responses, storage state, and bounded operation failures.

#### Scenario: Test disables a peripheral
- **WHEN** a scenario marks an emulated RTC, battery gauge, storage device, serial peer, or display device unavailable
- **THEN** the production application receives the corresponding bounded device error and follows its normal degraded behavior

### Requirement: Native-sim provides a virtual device-next USB host/device pair
The native-sim EF800Application target SHALL provide Zephyr device-next virtual UDC and UHC controllers connected through a software USB bus. EF800Application CDC and MTP contexts SHALL use the production device-next class implementations; the MTP context SHALL use the same storage roots as physical EF800Application. A native MTP stub that returns `-ENOTSUP` SHALL NOT satisfy this requirement.

#### Scenario: Native-sim MTP device enumerates through the software host
- **WHEN** the virtual USB host starts and EF800Application enables MTP
- **THEN** the host enumerates the production MTP device descriptors through the virtual bus and can issue device-next control and bulk transactions

#### Scenario: CDC class is exercised through the virtual bus
- **WHEN** EF800Application selects USB DebugConsole in native-sim
- **THEN** the software host enumerates the production CDC ACM class and exchanges test data through virtual USB endpoints

#### Scenario: Native-sim USB MSD is excluded by composition
- **WHEN** native-sim composition resolves to `USBMSD0`
- **THEN** the virtual USB personality set contains no EF800Application MSC context and the shared-data FAT remains available to EF800Application

#### Scenario: Native USB test is not treated as physical qualification
- **WHEN** a native-sim MTP transfer test passes
- **THEN** the result is reported as device-next protocol and filesystem behavior only, not Windows/Linux enumeration, STM32 USB electrical, or physical QSPI qualification

### Requirement: Model the selected battery SOC source in native-sim
Native-sim SHALL provide a source-independent battery SOC input from 0 through 100 percent and SHALL route it through the active BATSOC source model. The simulator SHALL represent an unavailable source separately from a valid 0-percent SOC value.

#### Scenario: Native-sim uses BATSOC2
- **WHEN** `BATSOC2` is active and the virtual battery is available
- **THEN** the MAX17048-compatible device path returns valid version, SOC, and VCELL readings, with its SOC response matching the configured virtual SOC input

#### Scenario: Native-sim uses BATSOC1
- **WHEN** `BATSOC1` is active and a valid calibration table is loaded
- **THEN** the Zephyr ADC emulator supplies a PC1-equivalent input voltage derived by inverse interpolation from the configured SOC percentage and the active table

#### Scenario: Native-sim uses BATSOC0
- **WHEN** `BATSOC0` is active
- **THEN** neither the MAX17048-compatible gauge nor the ADC model supplies a valid SOC reading to EF800Application

#### Scenario: Virtual SOC input is zero
- **WHEN** the browser sets the virtual SOC input to 0 percent while BATSOC1 or BATSOC2 is active
- **THEN** the selected emulated source reports a valid zero-percent reading instead of interpreting zero as an unset override

#### Scenario: Virtual battery state is reset
- **WHEN** the native-sim process restarts
- **THEN** transient browser battery input returns to its documented simulator default and does not alter physical target configuration
