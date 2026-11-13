# virtual-ef800-deployment Specification

## Purpose

Provide an independently deployable Docker Compose runtime for the Virtual EF800 simulator and localhost browser front panel.

## Requirements

### Requirement: Run Virtual EF800 independently of the development container
The system SHALL provide a dedicated minimal runtime image that runs the Linux Zephyr `native_sim` Virtual EF800 binary produced by `EF800_Devcontainer`, without using the long-lived development container as the runtime container. The runtime SHALL include the glibc dependencies required by the dynamically linked native-sim executable and SHALL supervise explicit simulator restarts.

#### Scenario: Build a deployable simulator image
- **WHEN** a developer builds the Virtual EF800 deployment from a clean repository checkout
- **THEN** the documented development build produces the native-sim executable and the runtime image packages that binary and its required runtime libraries without embedding the Zephyr workspace or compiler

#### Scenario: Start the simulator runtime
- **WHEN** the dedicated simulator container starts
- **THEN** it runs the Virtual EF800 `native_sim` executable as a non-root process and listens for its front-panel and bounded control clients on the Compose network

#### Scenario: Restart the simulator explicitly
- **WHEN** the simulator receives an accepted restart request after a package upload
- **THEN** it exits through the supervised restart path and the runtime starts a fresh native-sim process using the same virtual storage volume

### Requirement: Orchestrate the simulator and web front panel with Docker Compose
The system SHALL provide a documented Docker Compose application containing separate simulator and web-front-panel services on a private application network, with the web service able to upload packages and configure bounded virtual devices through the simulator control connection.

#### Scenario: Start the complete deployment
- **WHEN** a developer starts the documented Compose application
- **THEN** Compose starts the simulator and web-front-panel services and the web service reconnects until the simulator front-panel endpoint is available

#### Scenario: Stop the complete deployment
- **WHEN** a developer stops the Compose application
- **THEN** both services terminate without requiring the development container to be stopped or recreated

### Requirement: Restrict the interactive deployment to the local host
The Compose deployment SHALL publish only the web-front-panel HTTP endpoint on the Windows loopback interface and SHALL keep the simulator's raw TCP endpoint private to the Compose network.

#### Scenario: Open the front panel locally
- **WHEN** a developer opens the documented localhost URL
- **THEN** the browser receives the Virtual EF800 front panel from the web service

#### Scenario: Inspect published ports
- **WHEN** the Compose deployment is running with its default configuration
- **THEN** the host has no published simulator port and the web endpoint is bound to `127.0.0.1` rather than all host interfaces

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
