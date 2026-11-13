## 1. Characterize and Separate EF800Application Behavior

- [x] 1.1 Add characterization tests for current menu transitions, display composition, keyboard-host processing, plugin session navigation, and failure-screen behavior that the virtual target must preserve.
- [x] 1.2 Identify the minimal display, keyboard, storage, status, port, and process-lifetime seams in `ef800-application/src/main.c` and document which existing shared modules own each behavior.
- [x] 1.3 Extract reusable EF800Application orchestration for startup-independent menu, keyboard, display, plugin, and recovery behavior without changing physical EF800 results.
- [x] 1.4 Implement narrow physical adapters for any extracted seams and pass the existing native suites plus pristine EF800Application build before adding virtual adapters.

## 2. Establish the native_sim Virtual Target

- [x] 2.1 Add a dedicated virtual EF800 Zephyr application with `native_sim` configuration, source selection, and documented build/run commands inside `EF800_Devcontainer`.
- [x] 2.2 Start shared EF800Application orchestration with deterministic virtual status, clock, storage, and unavailable-device defaults while excluding unsupported physical USB, QSPI, watchdog, and boot-chain behavior.
- [x] 2.3 Add a headless startup test that verifies the virtual target initializes, remains running without a front panel, and reports its supported simulation capabilities.
- [x] 2.4 Verify virtual-target additions do not enter the four production firmware images and do not change the fixed image layout.

## 3. Implement the Virtual Display

- [x] 3.1 Refactor the reusable parts of the existing T6963 emulator/frame conversion so the virtual target can maintain a 240 x 64 monochrome display without replaying physical GPIO cycles.
- [x] 3.2 Implement the virtual display adapter with complete framebuffer snapshots, dirty-state coalescing, sequence numbers, and structured screen state for stable automated assertions.
- [x] 3.3 Add tests for text and graphics composition, header/body preservation, clear operations, frame dimensions and bit order, and malformed/partial frame rejection.
- [x] 3.4 Verify representative EF800Application menus and plugin screens produce the expected semantic state and selected pixel snapshots.

## 4. Add the Bidirectional Simulation Protocol

- [x] 4.1 Define a versioned length-delimited TCP protocol for hello/capabilities, LCD frames, key down/up/release-all, acknowledgements, simulator mode, peripheral controls, and observable state queries.
- [x] 4.2 Implement a bounded single-control-client server in the virtual target with reconnect handling, current-frame replay, message-size validation, and no-client headless operation.
- [x] 4.3 Bind the simulation endpoint to a configurable developer address/port and document Docker port publication between the reusable Linux container and Windows host.
- [x] 4.4 Add parser and integration tests covering fragmented messages, invalid versions/types/lengths, disconnects, reconnects, bounded queues, and latest-frame preservation under backpressure.

## 5. Implement the Virtual Keyboard and Keypad

- [x] 5.1 Add an in-process virtual keyboard-controller endpoint that emits initialization, resolves the existing semantic key names, and feeds normal make/break bytes through `ef800_keyboard_host`.
- [x] 5.2 Implement key-down, key-up, held-key tracking, release-all, deterministic repeat policy, and automatic release on disconnect or simulator reset.
- [x] 5.3 Add protocol-level tests for characters, modifiers, arrows, function/special keys, holds, repeats, invalid names, duplicate transitions, and release-all cleanup.
- [x] 5.4 Verify front-panel keys navigate EF800Application and plugin sessions only through existing keyboard translation and application handlers.

## 6. Build the Host Virtual Front Panel

- [x] 6.1 Separate the existing Tkinter framebuffer parser/renderer from serial-specific code and retain standalone TLX-711A viewer compatibility.
- [x] 6.2 Add a reconnecting TCP client that negotiates protocol version, displays connection/simulator mode, rejects malformed frames, and renders the latest 240 x 64 LCD state.
- [x] 6.3 Add a virtual EF800 keypad using the established semantic names and send distinct press/release events for mouse and mapped host-keyboard interaction.
- [x] 6.4 Ensure GUI networking runs outside Tk widget updates, queues are bounded, window close releases held keys, and the front panel remains responsive during simulator/plugin delays.
- [x] 6.5 Add headless tests for front-panel framing and state handling plus a documented manual Windows-to-container smoke test using native simulator real-time mode.

## 7. Add Deterministic Virtual Peripherals

- [x] 7.1 Implement disposable and explicitly persistent virtual storage modes, select either simulated-flash FAT or the backend-neutral persistence backend according to the design milestone, and prevent default access to physical EF800 images.
- [x] 7.2 Implement settable RTC, battery value/availability, VBUS/device availability, and bounded delay/error controls required by enabled EF800Application workflows.
- [x] 7.3 Add virtual serial transports and deterministic Elta3/Fennel1 peer scripts that exchange production protocol bytes and support timeout, truncation, malformed-response, and cleanup-failure scenarios.
- [x] 7.4 Report unsupported virtual workflows as explicit unavailable capabilities and test degraded startup rather than silently returning successful physical-device operations.

## 8. Exercise Plugin Threads and Recovery

- [x] 8.1 Add native healthy and adversarial test plugins for delayed completion, bounded non-return, stale generation, malformed service request, partial output, lifecycle failure, and host-service failure.
- [x] 8.2 Add scenarios proving the EF800Application UI remains schedulable and accepts supported keypad input while plugin work is active.
- [x] 8.3 Add timeout/recovery scenarios verifying output discard, workspace integrity, route cleanup, host-only abandonment, quarantine, diagnostic display, and parent-menu navigation.
- [x] 8.4 Verify an unrelated healthy plugin remains usable after safe recovery and verify cleanup uncertainty follows the configured escalation path.
- [x] 8.5 Label controlled virtual failures separately from native process crashes and retain STM32L496 MPU, W^X, stack-fault, and ARM exception tests as unresolved target gates.

## 9. Automate and Document the Virtual Platform

- [x] 9.1 Add a headless scenario client or test harness that can inject key/peripheral events, wait on sequenced state, and assert structured display and application outcomes without wall-clock sleeps.
- [x] 9.2 Add virtual target build and deterministic behavioral scenarios to CI without requiring a GUI or physical hardware.
- [x] 9.3 Run the complete native test suite, virtual scenarios, all four pristine production firmware builds, and `application/scripts/verify-images.sh`.
- [x] 9.4 Document interactive `--rt` operation, accelerated automated operation, front-panel startup, TCP/Docker configuration, supported virtual capabilities, and troubleshooting.
- [x] 9.5 Update `docs/testing.md` and `docs/migration-status.md` with the new virtual baseline and an explicit list of MCU, MPU, boot, electrical, QSPI, USB, and fitted-peripheral gates that virtual results do not close.
