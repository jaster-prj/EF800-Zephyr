# usb-personality-switching Specification

## Purpose

TBD - Define runtime switching and ownership rules for EF800 USB personalities.

## Requirements

### Requirement: Port 4 provides explicit USB personalities
The system SHALL represent no native-USB function, USB DebugConsole, USB MTP, and USB MSC as distinct Port 4 selections. Persisted identifiers for USB MTP SHALL be appended without changing the meaning of existing USB CDC or USB MSC assignments.

#### Scenario: Existing MSC assignment is loaded
- **WHEN** metadata written before MTP support assigns USB MSC to Port 4
- **THEN** the system interprets that assignment as USB MSC and does not reinterpret it as MTP

#### Scenario: MTP assignment survives restart
- **WHEN** the user selects USB MTP and the assignment is persisted successfully
- **THEN** the next boot selects USB MTP for Port 4

### Requirement: Non-MSC personalities switch without restarting
The system SHALL switch among None, USB DebugConsole, and USB MTP at runtime without restarting EF800 Application. A switch SHALL disable and shut down the current USB context before initializing the selected context, and SHALL expose only the selected USB class.

#### Scenario: DebugConsole changes to MTP
- **WHEN** USB DebugConsole is active and the user selects USB MTP
- **THEN** the host observes a USB disconnect followed by enumeration as an MTP device and EF800 Application continues running

#### Scenario: MTP changes to no USB personality
- **WHEN** USB MTP is active and the user removes the Port 4 assignment
- **THEN** the MTP session is terminated, native USB is disabled, FAT remains mounted, and EF800 Application does not restart

#### Scenario: MTP changes to DebugConsole
- **WHEN** USB MTP is active and the user selects USB DebugConsole
- **THEN** the host observes re-enumeration as CDC DebugConsole without an MTP or MSC interface and EF800 Application does not restart

### Requirement: MSC transitions use a restart boundary
The system SHALL require a controlled restart for every personality transition that enters or leaves USB MSC. The persisted assignment SHALL be updated successfully before restart, and EF800 Application SHALL never remount FAT after an active MSC worker in the same boot.

#### Scenario: MTP changes to MSC
- **WHEN** USB MTP is selected and the user selects USB MSC
- **THEN** the system persists USB MSC, reports that restart is required, and restarts before transferring the disk from FAT to MSC

#### Scenario: MSC changes to MTP
- **WHEN** USB MSC is selected and the user selects USB MTP
- **THEN** the system persists USB MTP, reports that restart is required, and restarts before mounting FAT and starting MTP

#### Scenario: MSC selection persistence fails
- **WHEN** a transition involving USB MSC cannot be persisted
- **THEN** the system does not restart and retains the previous assignment and ownership model

### Requirement: Runtime USB transitions are serialized and recoverable
The system SHALL serialize USB personality transitions and SHALL prevent two USB contexts from owning the STM32 USB device controller concurrently. If a non-MSC activation fails, the system SHALL report the error and either restore the prior non-MSC personality or enter the None state without changing persisted state to a personality that is not active.

#### Scenario: MTP activation fails
- **WHEN** the user selects USB MTP and its context cannot be initialized or enabled
- **THEN** the system reports the failure, does not expose concurrent CDC or MSC classes, and keeps persisted and runtime state consistent

#### Scenario: Repeated selection occurs during transition
- **WHEN** another Port 4 selection is requested while a USB transition is in progress
- **THEN** the USB manager serializes or rejects the request without operating two contexts concurrently

### Requirement: USB selection remains well-defined without host VBUS
The system SHALL accept and persist a non-MSC USB personality selection while no host is attached, and SHALL expose that personality when VBUS subsequently becomes available without requiring a restart.

#### Scenario: MTP is selected while disconnected
- **WHEN** the user selects USB MTP with no USB host attached
- **THEN** the selection succeeds without restart and the device enumerates as MTP when a host is later attached
