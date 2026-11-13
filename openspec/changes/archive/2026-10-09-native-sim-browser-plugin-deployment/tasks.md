## 1. Native-sim PMP package profile

- [x] 1.1 Define native-sim PMP target metadata and package validation rules distinct from physical ARM packages.
- [x] 1.2 Extend the measurement-plugin build system to produce native-sim ELF/PMP artifacts for at least one demonstration plugin while preserving ARM package targets.
- [x] 1.3 Add the native-sim PluginManager entry/loader adapter and reject physical ARM packages before execution.
- [x] 1.4 Add package validation tests for valid native packages, ARM-target rejection, malformed manifests, duplicate IDs, size limits, and contract incompatibility.

## 2. Simulator package-control and restart protocol

- [x] 2.1 Define bounded protocol messages for upload begin, upload chunk, upload commit, package status, restart request, acknowledgements, and errors.
- [x] 2.2 Implement ordered chunk handling with upload IDs, offsets, total-size limits, digest verification, timeout cleanup, and no partial final files.
- [x] 2.3 Implement atomic temporary-file installation into the virtual FAT plugin directory and validate packages before rename.
- [x] 2.4 Add supervised simulator restart behavior that releases plugin sessions and virtual ports, preserves the flash volume, and reruns EF800Application startup discovery.
- [x] 2.5 Expose package inventory, validation diagnostics, restart-required state, and post-restart registry state through the simulator control protocol.

## 3. Browser gateway and UI

- [x] 3.1 Add a bounded localhost multipart upload endpoint with safe filenames, native-sim target checks, request-size limits, and upload timeouts.
- [x] 3.2 Extend the gateway's single simulator connection with package-control request/response handling and progress broadcasts.
- [x] 3.3 Add browser plugin inventory, upload progress, validation errors, restart-required state, and explicit Restart simulator controls.
- [x] 3.4 Preserve controller ownership, release-all behavior, private simulator networking, and existing LCD/keypad browser functionality.
- [x] 3.5 Add gateway unit tests for valid/invalid uploads, interrupted transfers, controller authorization, restart state, and reconnect behavior.

## 4. Virtual device docking

- [x] 4.1 Define bounded virtual-device profile messages for port assignment, serial framing, availability, delay, bounded errors, and request/response scripts.
- [x] 4.2 Route profile updates to the existing native-sim serial-peer/profile emulators and return deterministic acknowledgements and state.
- [x] 4.3 Add browser virtual-device docking controls and diagnostics without exposing filesystem paths, raw UART handles, or unrestricted simulator internals.
- [x] 4.4 Verify a native-sim Measurement plugin exchanges data through the production host-owned Port service with a docked virtual instrument.

## 5. End-to-end validation and documentation

- [x] 5.1 Add an end-to-end native-sim demonstration package and scenario covering upload, explicit restart, discovery, assignment, device docking, plugin execution, and LCD/UI output.
- [x] 5.2 Test package upload and activation across simulator restart with persistent virtual FAT storage and failed-upload recovery.
- [x] 5.3 Build and run the full native-sim Twister suite, plugin package tests, gateway tests, and required firmware regression builds.
- [x] 5.4 Update Compose, browser deployment, plugin contract, and native-sim documentation with the upload API, restart workflow, native-package boundary, and behavioral-evidence limitations.
- [x] 5.5 Record unsupported production security and ARM-qualification items as explicit release gates rather than treating native-sim package success as hardware evidence.
