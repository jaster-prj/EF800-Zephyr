# Use a Bounded Native-Sim Log Stream Through the Front-Panel Gateway

- Status: accepted
- Date: 2026-10-08
- Supersedes: None

## Context

The Virtual EF800 uses a single gateway-owned binary front-panel connection for
LCD state, keypad input, plugin control, and virtual-device administration. The
production EF800 Application logging backend currently writes through the EF800
console path, leaving browser users without startup and runtime diagnostics. A
second simulator connection would conflict with the single-client boundary in
ADR-0004, while direct socket writes from logging code could block application
threads or recursively fail when the browser disconnects.

The browser deployment is local and native-sim-only. Its diagnostics must remain
bounded, must not become durable application state, and must not change physical
EF800 console behavior or the hardware qualification boundary.

## Decision

Native-sim will expose a separate bounded diagnostic sink alongside the existing
console log path. Log producers will enqueue bounded records into native-sim-owned
queue/history storage without writing to the socket. The existing front-panel worker
will be the sole sender of a versioned log-record message over the gateway-owned
connection.

The simulator will retain a small recent history and replay it in sequence order on
connection before live records. Records will carry monotonic sequence numbers and
bounded severity/text metadata; overflow and reconnect gaps will remain observable.
The gateway will translate the stream to the browser WebSocket, where rendering and
filters remain browser concerns. The feature will not be enabled for physical
firmware console routing.

## Consequences

- Browser users can inspect EF800 Application startup and runtime diagnostics without
  opening a separate native-sim console path.
- Logging remains non-blocking with respect to browser availability and socket
  failures.
- The existing one-client simulator boundary and gateway ownership model remain
  intact.
- Recent history and delivery are intentionally bounded; this is not a durable log
  or guaranteed-delivery service.
- A new internal protocol message and native-sim queue/history implementation must
  be tested alongside existing LCD, keypad, plugin, and restart traffic.
- Physical console behavior and all hardware-specific qualification requirements
  remain unchanged.
