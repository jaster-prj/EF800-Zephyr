## Why

The Virtual EF800 browser front panel currently exposes LCD state, keypad control,
and selected plugin/device status, but not the EF800Application diagnostic logs that
explain startup, plugin discovery, port assignment, and runtime failures. Developers
must therefore inspect a separate console path, which is especially inconvenient
for the containerized native-sim deployment. A bounded browser log stream will make
behavioral debugging observable through the same localhost front panel without
coupling EF800Application execution to browser connectivity.

## What Changes

- Add a native-sim-only, bounded EF800Application log stream to the existing
  simulator front-panel protocol.
- Retain a small recent log history and replay it to a newly connected gateway
  client before continuing with live records.
- Preserve log sequence numbers and report truncation, dropped records, and
  reconnect/discontinuity state without blocking application threads.
- Forward log records through the existing gateway WebSocket to connected browser
  viewers.
- Add a browser diagnostics panel with bounded rendering, severity filtering,
  pause/clear controls, auto-scroll behavior, and dropped-record indicators.
- Add protocol, native front-panel, gateway, web-asset, and integration coverage
  for history replay, live delivery, queue bounds, malformed records, disconnects,
  and transport contention.
- Keep physical EF800 console/log behavior unchanged and identify the browser
  stream as behavioral native-sim diagnostics.

## Capabilities

### New Capabilities

- `virtual-ef800-diagnostics`: Bounded native-sim EF800Application log history and
  live browser diagnostics stream.

### Modified Capabilities

- None. The diagnostics stream is additive and does not change the existing LCD,
  keypad, plugin, or deployment contracts.

## Impact

- Native-sim logging backend and virtual front-panel transport in `shared/`.
- Versioned protocol definitions and Python gateway handling under
  `GSE/Virtual-EF800/`.
- Browser HTML, CSS, and JavaScript for the diagnostics panel.
- Native-sim, gateway, web-asset, and Compose integration tests.
- No physical firmware console routing, hardware protocol, or externally published
  simulator port changes.
