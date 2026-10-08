# Use a Web Gateway for the Virtual EF800 Front Panel

- Status: accepted
- Date: 2026-10-07
- Supersedes: None

## Context

ADR-0003 establishes Zephyr `native_sim` as the behavioral EF800 simulator and a bounded interface shared by interactive and automated clients. The first interactive client is a Windows Tkinter process that connects directly to the simulator's single-client binary TCP endpoint. That arrangement couples operation to a host Python GUI, manual port publication, and the long-lived development container, and it cannot safely coordinate multiple browser sessions.

Virtual EF800 needs an independently deployable interactive surface without adding HTTP presentation concerns to the Zephyr application or exposing its internal binary protocol directly to browsers. The simulator must remain usable by direct headless clients when the interactive deployment is not running.

## Decision

The interactive Virtual EF800 deployment will use two separately managed services: a dedicated `native_sim` runtime container and a web gateway. The gateway will own the simulator's one binary front-panel connection, translate retained display and semantic keypad state to a bounded browser WebSocket protocol, and serve a browser front panel. The browser will not connect directly to the simulator.

The gateway will support multiple viewers but at most one explicit controller lease. Loss of the controller or simulator connection will clear ownership and release held input. The default developer deployment will expose only the web endpoint on host loopback and will keep simulator TCP private. Direct automated clients remain compatible by running without the gateway.

## Consequences

- Interactive operation no longer requires Tkinter, graphical forwarding, or use of the persistent development container as the simulator runtime.
- Browser presentation, connection recovery, controller arbitration, and HTTP health become gateway responsibilities rather than Zephyr application responsibilities.
- The simulator protocol remains a bounded internal and headless-test contract, while the browser protocol can evolve independently under explicit versioning.
- Compose or equivalent orchestration must preserve one gateway per simulator and avoid health probes that consume the simulator's client connection.
- Remote multi-user deployment remains a separate security decision; loopback binding is not an authentication mechanism for later network exposure.
