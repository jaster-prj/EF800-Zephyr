# virtual-ef800-deployment Specification

## Purpose

Provide an independently deployable Docker Compose runtime for the Virtual EF800 simulator and localhost browser front panel.

## Requirements

### Requirement: Run Virtual EF800 independently of the development container
The system SHALL provide a dedicated Alpine runtime image that runs the Linux Zephyr `native_sim` Virtual EF800 binary produced by `EF800_Devcontainer`, without using the long-lived development container as the runtime container.

#### Scenario: Build a deployable simulator image
- **WHEN** a developer builds the Virtual EF800 deployment from a clean repository checkout
- **THEN** the documented development build produces the native-sim executable and the Alpine image packages that binary without embedding the Zephyr workspace or compiler

#### Scenario: Start the simulator runtime
- **WHEN** the dedicated simulator container starts
- **THEN** it runs the Virtual EF800 `native_sim` executable as its foreground process and listens for its front-panel client on the Compose network

### Requirement: Orchestrate the simulator and web front panel with Docker Compose
The system SHALL provide a documented Docker Compose application containing separate simulator and web-front-panel services on a private application network.

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
