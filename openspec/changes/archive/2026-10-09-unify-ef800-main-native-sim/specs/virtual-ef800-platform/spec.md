## MODIFIED Requirements

### Requirement: Run EF800 behavior as a native simulator application
The system SHALL provide a buildable and runnable Zephyr `native_sim` application by building `application/ef800-application` for the `native_sim` board, executing the same production EF800Application composition root used by the physical `ef800` target, and substituting required physical EF800 devices with Zephyr-compatible emulated or simulated peripherals.

#### Scenario: Start the virtual EF800
- **WHEN** a developer builds and starts `application/ef800-application` for `native_sim`
- **THEN** the production application initializes without an STM32 MCU or physical EF800 and exposes its virtual front-panel interface

#### Scenario: Run concurrent plugin behavior
- **WHEN** EF800Application starts a plugin operation on a worker thread
- **THEN** the virtual target uses Zephyr scheduling and synchronization primitives and keeps the EF800Application UI path independently schedulable

## ADDED Requirements

### Requirement: Keep physical and native-sim production source composition aligned
The physical `ef800` and native-sim builds SHALL consume one shared production source manifest, with target differences limited to explicitly selected configuration, devicetree, and platform-adapter sources.

#### Scenario: A shared application source changes
- **WHEN** a production EF800Application source is added to the shared source manifest
- **THEN** both the physical EF800 and native-sim builds include that source without editing two application wrappers

#### Scenario: Native-only adapter is selected
- **WHEN** `application/ef800-application` is built for `native_sim`
- **THEN** native peripheral adapters are selected while physical STM32 adapters remain excluded from the native image
