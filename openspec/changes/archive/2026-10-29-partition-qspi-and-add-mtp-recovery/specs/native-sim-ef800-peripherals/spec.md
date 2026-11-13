## ADDED Requirements

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
