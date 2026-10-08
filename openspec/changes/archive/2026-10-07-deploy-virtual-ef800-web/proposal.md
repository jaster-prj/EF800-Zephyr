## Why

The Virtual EF800 currently runs from the reusable development container and depends on a Windows-hosted Tkinter client plus manual TCP port forwarding. This couples interactive simulation to the development workspace and prevents a repeatable, browser-based deployment that can be started and stopped independently.

## What Changes

- Build the Zephyr `native_sim` Virtual EF800 in `EF800_Devcontainer`, then package only its Linux executable in a dedicated Alpine runtime container that runs independently of `EF800_Devcontainer`.
- Replace the Tkinter front panel with a localhost-only web service that owns the simulator's binary TCP connection, serves a browser UI, and bridges display and keypad traffic over WebSocket.
- Provide Docker Compose orchestration for the simulator and web service, exposing only the browser endpoint on `127.0.0.1` while keeping the raw port `8800` private to the Compose network.
- Support multiple browser viewers with at most one active controller, including safe key release when control or connectivity is lost.
- Add container, protocol-gateway, browser-interface, and Compose lifecycle tests and update Virtual EF800 operating documentation.
- Retain the headless scenario client and behavioral-versus-hardware qualification boundary.

## Capabilities

### New Capabilities

- `virtual-ef800-deployment`: Reproducible, isolated Docker image and Compose lifecycle for the simulator and localhost web front panel.

### Modified Capabilities

- `virtual-ef800-platform`: Replace the desktop Tkinter interaction path with a browser front panel mediated by a reconnecting web gateway, including viewer/controller ownership and disconnect safety.

## Impact

- Affected areas include `application/ef800-virtual`, `GSE/Virtual-EF800`, container build and Compose configuration, host-side protocol tests, CI, and Virtual EF800 documentation.
- The browser-facing interface adds an HTTP/WebSocket service and static web assets; the existing versioned simulator TCP protocol remains internal and compatible with the headless scenario client.
- `EF800_Devcontainer` remains the reusable development environment but is no longer the runtime container used by the interactive Virtual EF800 deployment.
- The first deployment is intentionally localhost-only and does not add remote authentication, TLS, or LAN access.
