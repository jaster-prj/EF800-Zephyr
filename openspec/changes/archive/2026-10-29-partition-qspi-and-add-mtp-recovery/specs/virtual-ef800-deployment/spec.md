## ADDED Requirements

### Requirement: Gateway mediates browser MTP operations through the simulator
The web gateway SHALL provide bounded file-list, upload, download, and transfer-status operations for the native-sim EF800Application MTP peer over the existing private simulator control connection. The browser SHALL connect only to the gateway and SHALL NOT connect directly to the simulator or USB host controller.

#### Scenario: Browser starts an MTP transfer
- **WHEN** a connected browser requests an MTP file operation
- **THEN** the gateway validates the operation and sends bounded control/data messages to the simulator's in-process USB host peer

#### Scenario: Transfer exceeds a configured bound
- **WHEN** a browser requests a file operation exceeding configured size, path, or message bounds
- **THEN** the gateway rejects it before starting a USB transaction and reports a bounded error

#### Scenario: Simulator disconnects during a transfer
- **WHEN** the simulator connection is lost during an MTP operation
- **THEN** the gateway marks the operation failed, clears transfer state, and does not claim that a partial file was completed
