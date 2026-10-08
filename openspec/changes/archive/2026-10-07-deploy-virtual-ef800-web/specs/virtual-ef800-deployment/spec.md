## ADDED Requirements

### Requirement: Run Virtual EF800 independently of the development container
The system SHALL provide a dedicated Alpine runtime image that runs the Linux Zephyr `native_sim` Virtual EF800 binary produced by `EF800_Devcontainer`, without using the long-lived development container as the runtime container.

#### Scenario: Build a deployable simulator image
- **WHEN** a developer builds the Virtual EF800 deployment from a clean repository checkout
- **THEN** the documented development build produces `application/build/ef800-virtual/zephyr/zephyr.exe` in `EF800_Devcontainer`, and the Alpine image packages that binary without embedding the Zephyr workspace or compiler

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

#### Scenario: Restart one service
- **WHEN** either the simulator or web-front-panel service restarts
- **THEN** the other service remains independently manageable and the web gateway restores its simulator connection when both services are available

### Requirement: Restrict the interactive deployment to the local host
The Compose deployment SHALL publish only the web-front-panel HTTP endpoint on the Windows loopback interface and SHALL keep the simulator's raw TCP endpoint private to the Compose network.

#### Scenario: Open the front panel locally
- **WHEN** a developer opens the documented localhost URL
- **THEN** the browser receives the Virtual EF800 front panel from the web service

#### Scenario: Inspect published ports
- **WHEN** the Compose deployment is running with its default configuration
- **THEN** the host has no published simulator port and the web endpoint is bound to `127.0.0.1` rather than all host interfaces

### Requirement: Report deployment readiness without consuming the simulator client connection
The deployment SHALL expose web-service health that distinguishes HTTP availability from simulator connectivity and SHALL NOT probe the simulator by opening competing TCP front-panel connections.

#### Scenario: Simulator connection is healthy
- **WHEN** the gateway has a live protocol connection to the simulator
- **THEN** its health response reports the simulator connection as available

#### Scenario: Simulator is unavailable
- **WHEN** the web service is running but cannot connect to the simulator
- **THEN** the web endpoint remains available, reports the simulator as unavailable, and continues bounded reconnection attempts

#### Scenario: Container health is evaluated
- **WHEN** Docker evaluates service health
- **THEN** no health check takes ownership of the simulator's single front-panel connection

### Requirement: Preserve repeatable runtime compatibility
The simulator build SHALL use the manifest-pinned Zephyr workspace in `EF800_Devcontainer`, and the Alpine runtime image SHALL verify that the produced native executable can start with only its required compatibility libraries.

#### Scenario: Build inputs change
- **WHEN** the manifest revision or container base reference changes
- **THEN** the simulator image is rebuilt from the declared inputs rather than relying on a binary left in a host build directory

#### Scenario: Validate the runtime image
- **WHEN** the simulator image is tested
- **THEN** a container smoke test starts the packaged executable and observes Virtual EF800 initialization on the intended runtime architecture
