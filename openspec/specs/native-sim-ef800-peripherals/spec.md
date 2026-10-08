# native-sim-ef800-peripherals Specification

## Purpose

Provide Zephyr-compatible emulated and simulated peripherals that let the production EF800 MainApplication run on native_sim with deterministic healthy and failure profiles.

## Requirements

### Requirement: Provide native peripherals through the shared MainApplication target
The native-sim GPIO, UART, I2C, storage, RTC, battery, LCD, keyboard, profile, and front-panel emulation SHALL remain available when `application/ef800-main` is built for `native_sim`, without requiring an `application/ef800-virtual` application wrapper.

#### Scenario: Build the shared target for native_sim
- **WHEN** a developer builds `application/ef800-main` with `-b native_sim`
- **THEN** the production source manifest links with the native peripheral adapters and starts the same MainApplication composition root

#### Scenario: Physical build remains hardware-specific
- **WHEN** a developer builds `application/ef800-main` with `-b ef800`
- **THEN** the build selects STM32, physical USB, QSPI, watchdog, and board devicetree adapters without linking native-sim emulation code

### Requirement: Remove the duplicate virtual application model
The project SHALL not require a second simulator-specific MainApplication or menu implementation after the shared target supports native_sim.

#### Scenario: Virtual application wrapper is removed
- **WHEN** the native-sim target is built and launched
- **THEN** its entry point and application workflows originate from `application/ef800-main`, while retained virtual modules are limited to peripheral, transport, and front-panel infrastructure

### Requirement: Provide a healthy native-sim peripheral profile
The native-sim EF800 SHALL provide a default profile in which the GPIO banks, UART devices, I2C devices, simulated storage, RTC, battery gauge, keyboard controller, and LCD device are present and operational so the production MainApplication startup path can complete without physical hardware.

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
