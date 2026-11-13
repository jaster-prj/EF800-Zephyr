## Context

The native-sim Virtual EF800 already runs the production EF800Application, exposes a localhost browser front panel, and provides virtual serial peers and peripheral profiles. PluginManager discovery is startup-only and the physical plugin build currently targets ARM. The browser gateway currently owns one simulator connection for LCD and keypad traffic but has no package-management or virtual-device administration surface.

The simulator's storage is a Zephyr virtual FAT volume backed by a flash image inside the runtime container. A Linux Docker bind mount is not the same filesystem as the Zephyr FAT volume and cannot directly populate the production plugin scan path. Native-sim plugin deployment must therefore cross the existing simulator control boundary and write packages through the simulator's own filesystem path.

The existing plugin lifecycle deliberately avoids hot replacement. The safe activation boundary is an explicit simulator restart followed by normal EF800Application startup discovery. This change extends that lifecycle for native-sim only; it does not change physical USB ownership or claim ARM package qualification.

## Goals / Non-Goals

**Goals:**

- Build native-sim-compatible PMP packages with the existing EF800 contract and a target-specific native executable profile.
- Upload packages from the localhost browser through the gateway and bounded simulator control protocol.
- Validate and install packages atomically into the virtual FAT plugin directory.
- Keep the running registry unchanged until the user presses a separate restart button.
- Restart the simulator through a supervised runtime path and rediscover packages at startup.
- Configure bounded virtual serial instruments from the browser and dock them to plugin ports.
- Demonstrate a native-sim plugin exchanging data with an emulated device and rendering bounded UI content.
- Keep all simulator and package-control endpoints private to the Compose network.

**Non-Goals:**

- Hot loading, hot replacement, filesystem monitoring, or live migration of plugin sessions.
- Uploading or executing physical ARM PMP packages in native_sim.
- Changing physical EF800 USB MSD/CDC/none ownership or plugin discovery behavior.
- Production package signatures, anti-rollback, MPU containment, ARM fault qualification, or arbitrary remote deployment.
- Giving plugins direct filesystem, GPIO, DeviceTree, T6963, raw UART, or thread-creation access.
- Publishing the simulator's raw control protocol outside the private Compose network.

## Decisions

### Use a native-sim-specific PMP target profile

The plugin SDK build will produce a native-sim package variant with a native ELF payload and target metadata. Physical ARM packages remain separate artifacts. The EF800 host will select a native entry/loader adapter when built for `native_sim` and retain the ARM adapter for physical builds.

Alternatives considered:

- Execute ARM packages through QEMU/Renode: rejected for this behavioral increment because it would turn native-sim into an ARM execution environment and add a second emulator stack.
- Run plugins as separate host subprocesses: deferred; IPC containment and lifecycle would be a larger architectural change than native package loading.
- Treat physical ARM packages as native: rejected because incompatible code could crash the simulator and would produce misleading evidence.

### Extend the existing simulator control connection

The gateway will continue to own the simulator's single TCP connection. The binary protocol will gain bounded management messages for package upload, package status, restart request, virtual-device profile updates, and state acknowledgements. Upload payloads will be chunked below the protocol maximum and identified by an upload ID, total length, offset, and digest.

This avoids granting the gateway Docker-socket access and keeps all simulator state changes behind one serialized control boundary.

### Use HTTP upload plus browser status messages

The gateway will expose an HTTP multipart upload endpoint for packages and JSON/WebSocket status messages for progress, validation, restart-required state, registry state, and virtual-device state. The existing keypad/LCD WebSocket remains the interactive surface; package data does not travel as unbounded browser JSON.

The gateway will enforce localhost deployment, package size limits, safe filenames, native-sim target selection, and upload timeouts before forwarding chunks.

### Install through virtual FAT, not a host bind mount

The simulator will write an upload to a temporary file in the configured plugin directory, validate the complete package, and atomically rename it to the final `.PMP` path. A Docker-mounted staging directory may be useful for diagnostics, but it will not be the production discovery path because it bypasses the Zephyr FAT volume.

### Make restart explicit and supervised

The browser will show a separate Restart simulator action after a successful upload. The simulator will stop accepting new plugin work, release active sessions and virtual ports, flush storage, and exit with a restart status. The runtime entrypoint will restart the native_sim process while preserving the virtual flash volume. EF800Application then performs its existing startup-only plugin scan.

### Keep virtual device docking declarative and bounded

The browser will configure named virtual serial profiles with port assignment, availability, baud/framing defaults, delay, bounded errors, and request/response scripts. The profile state will be acknowledged and exposed to the browser. It will feed the existing virtual serial peer and host-owned Port service rather than bypassing production plugin code.

### Preserve behavioral evidence boundaries

The browser may label a package as native-sim compatible and show discovery/activation results, but the deployment will clearly identify results as behavioral evidence. Successful native-sim PMP execution will not qualify ARM loading, MPU protection, package authenticity, or physical device behavior.

## Risks / Trade-offs

- [Native package loading can diverge from physical ARM package loading] -> Keep separate target profiles, shared contract tests, and explicit behavioral-only reporting.
- [A malformed upload could corrupt the virtual FAT] -> Use bounded chunk state, temporary files, digest validation, package parsing before commit, and atomic rename.
- [Restart can interrupt an active browser session] -> Require explicit user action, release active keys/ports first, preserve the last browser status, and reconnect with bounded backoff.
- [The gateway's single simulator connection can be blocked by a large upload] -> Chunk uploads, serialize control operations, bound chunk and total sizes, and publish progress/timeout state.
- [Virtual instruments may hide production port mistakes] -> Route every exchange through the production Port service and expose availability, delay, and error profiles in test assertions.
- [Localhost upload still installs executable code] -> Keep the endpoint loopback-only, reject non-native packages, avoid Docker socket access, and retain production signing/security work as a separate gate.
- [Plugin directory conventions differ between current implementation and documentation] -> Select one explicit native-sim plugin path and use it consistently for upload, FAT scan, tests, and documentation.

## Migration Plan

1. Define the native-sim PMP build profile and target metadata validation.
2. Add native-sim loader/entry adaptation while preserving the physical ARM path.
3. Extend the simulator protocol with bounded package, restart, and virtual-device control messages.
4. Add gateway upload/status/restart APIs and browser management panels.
5. Add simulator-side atomic FAT installation and supervised restart handling.
6. Add virtual instrument docking backed by existing serial-peer/profile emulators.
7. Build a native-sim demonstration plugin, upload it through the browser, restart, assign it to a docked device, and verify its LCD/UI output.
8. Run package validation, gateway, native-sim integration, full Twister, Compose, and firmware regression tests.

Rollback removes the browser management routes and simulator control messages while retaining the existing LCD/keypad gateway. Existing physical plugin packages and startup behavior remain unchanged.

## Open Questions

- Whether the native-sim package profile should use the host process ABI directly or a stable 32-bit native ABI wrapper shared by all supported host environments.
- Whether package inventory and virtual-device state should survive simulator restart in virtual FAT metadata or be treated as browser/controller configuration.
- Whether a package removal operation is needed in the first browser milestone, or whether upload and restart are sufficient.
