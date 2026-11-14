## ADDED Requirements

### Requirement: Provide native peripherals through the shared EF800 Application target
The native-sim GPIO, UART, I2C, storage, RTC, battery, LCD, keyboard, profile, and front-panel emulation SHALL remain available when `application/ef800-application` is built for `native_sim`, without requiring an `application/ef800-virtual` application wrapper.

#### Scenario: Build the shared target for native_sim
- **WHEN** a developer builds `application/ef800-application` with `-b native_sim`
- **THEN** the production source manifest links with the native peripheral adapters and starts the same EF800 Application composition root

#### Scenario: Physical build remains hardware-specific
- **WHEN** a developer builds `application/ef800-application` with `-b ef800`
- **THEN** the build selects STM32, physical USB, QSPI, watchdog, and board devicetree adapters without linking native-sim emulation code

### Requirement: Remove the duplicate virtual application model
The project SHALL not require a second simulator-specific EF800 Application or menu implementation after the shared target supports native_sim.

#### Scenario: Virtual application wrapper is removed
- **WHEN** the native-sim target is built and launched
- **THEN** its entry point and application workflows originate from `application/ef800-application`, while retained virtual modules are limited to peripheral, transport, and front-panel infrastructure
