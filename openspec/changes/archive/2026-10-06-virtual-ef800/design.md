## Context

EF800 currently has strong native coverage for extracted shared components and physical acceptance paths for EF800Application, but no product-level simulator that combines the real menu flow, plugin lifecycle, Zephyr threads, display state, keyboard interaction, persistence, and serial peers. Production EF800Application is also tightly connected to concrete devicetree GPIO, UART, I2C, flash, USB, watchdog, and RTC devices. Running its unchanged STM32 image would therefore require a large SoC-emulation project that is unnecessary for the desired behavioral coverage.

Useful building blocks already exist. `native_sim` provides Zephyr scheduling, simulated flash, GPIO and bus emulators, PTY UARTs, RTC facilities, and host integration. The repository has a T6963 behavioral model and a Tkinter 240 x 64 framebuffer viewer under `application/tlx-711a` and `GSE/TLX-711A`, a semantic keyboard key-name vocabulary with press/release behavior, host-side serial instrument dummies, and backend-neutral domain components. The virtual platform must preserve the package-owned logical UI from ADR-0001 and exercise the supervised plugin threading from ADR-0002 without claiming to verify that ADR's STM32 MPU and ARM fault mechanisms.

The normal development environment runs Zephyr inside a Linux devcontainer mounted from Windows. A host front panel must therefore avoid depending on an SDL or X11 window emitted directly by the container.

## Goals / Non-Goals

**Goals:**

- Run product-level EF800Application, plugin, menu, persistence, and concurrency behavior as a Zephyr `native_sim` executable.
- Provide a live 240 x 64 LCD and a clickable EF800 keypad for manual development.
- Use the same bidirectional control surface for deterministic automated acceptance scenarios.
- Preserve normal keyboard handling and plugin/session paths instead of directly manipulating menus or package state.
- Model storage, time, battery, serial peers, VBUS/device availability, delays, and bounded failures at their observable behavioral boundaries.
- Keep the virtual target maintainable by sharing production domain and orchestration code and making hardware substitution explicit.
- Clearly separate behavioral evidence from hardware and security qualification.

**Non-Goals:**

- Emulating STM32L496 or STM32L431 registers, clocks, interrupts, or exact instruction execution.
- Running the production ARM ELF, bootloader chain, BackupApplication, or L431 binary unchanged.
- Proving MPU W^X mappings, ARM fatal-reason classification, physical fault containment, or arbitrary native-code safety.
- Simulating electrical levels, oscillator accuracy, bus signal integrity, QSPI geometry, USB electrical behavior, or PCB wiring.
- Reproducing every peripheral in the first increment when it is not required by an enabled behavioral scenario.
- Replacing the existing physical release gates.

## Decisions

### Add a dedicated virtual application target on native_sim

Create a separate virtual EF800 application target rather than making the STM32 board definition pretend to be virtual. It will link production domain, menu, plugin, supervisor, persistence, and port code where those components can run independently of physical drivers, and it will select virtual adapters at build time for physical boundaries.

This keeps production hardware configuration intact and permits native-only dependencies without entering embedded images. The virtual executable remains a real Zephyr application, so plugin and UI threads use `k_thread`, queues, semaphores, mutexes, priorities, and simulated time rather than a desktop reimplementation of scheduling.

Alternatives considered:

- Add an STM32L496 QEMU machine: rejected for this capability because peripheral-register implementation dominates the work without improving source-level plugin behavior tests.
- Compile the full current `ef800-application/src/main.c` unchanged against fake devicetree devices: rejected as the sole approach because direct hardware setup and the 3,800-line application entry point would force broad low-level emulation and make behavioral failure injection awkward.
- Implement the EF800 UI entirely in Python: rejected because it would not exercise production Zephyr threads or application control flow.

### Extract orchestration seams, not a second application model

Move only hardware-facing and process-lifetime decisions behind narrow EF800-owned interfaces as needed. EF800Application and the virtual target must share menu transitions, keyboard event handling, plugin sessions, worker supervision, recovery policy, persistence semantics, and display composition. Physical adapters continue to own GPIO/UART/I2C/flash operations; virtual adapters own deterministic state and test controls.

The preferred dependency direction is:

```text
EF800Application behavior
        |
        +--> display surface --> physical T6963 adapter / virtual display
        +--> keyboard source --> L431 UART adapter / virtual keyboard
        +--> storage backend --> QSPI FAT adapter / virtual storage
        +--> port transport --> STM32 UART adapter / virtual serial peer
        +--> status source ----> DS3231/MAX17043 / virtual clock+battery
```

Interfaces should remain focused on observable operations already needed by the application. They must not become a generic hardware abstraction layer or introduce compatibility fallbacks into production.

Alternatives considered:

- Emulate all GPIO and I2C transactions beneath unchanged drivers: useful for driver testing, but too detailed for the primary behavior goal and costly for the parallel T6963 bus.
- Fork EF800Application logic for the virtual target: rejected because simulator-only behavior would drift and lose acceptance value.

### Use a host-side front panel over a versioned TCP protocol

Run the graphical front panel on Windows and the virtual EF800 in the container. Use a localhost-published TCP endpoint with a small versioned, length-delimited binary protocol. The protocol carries an initial hello/capabilities exchange, complete LCD frames, semantic key-down/key-up/release-all commands, acknowledgements, and bounded test-control requests. Display frames contain dimensions, monotonically increasing sequence numbers, payload length, and packed 1-bit pixels; malformed or partial frames are discarded.

TCP is available across Docker port publishing, supports both directions, and avoids container GUI forwarding and serial-port ownership problems. One active control client is sufficient initially; reconnect sends current display state and resets held input. Automated clients use the same protocol without starting a GUI.

The existing Tkinter viewer is extended into the first front panel because it already renders the exact packed 240 x 64 format. Transport parsing is separated from widgets so it can be tested headlessly. The existing `FB: [...]` serial envelope may remain supported by the standalone TLX-711A tool but is not used as the new bidirectional protocol because delimiter framing raw binary is fragile and has no input/control channel.

Alternatives considered:

- SDL display inside `native_sim`: Zephyr supports it, but forwarding a container window to Windows complicates the standard environment and does not provide a complete keypad/control protocol.
- WebSocket/browser UI: viable later, but adds server and browser assets when the existing Tkinter renderer gives a smaller first increment.
- PTY UART: appropriate for serial-device peers but inconvenient as the sole Windows/container front-panel transport.

### Render from a virtual display model at the application boundary

The virtual display keeps a 240 x 64 monochrome framebuffer plus enough text/display state for stable automated assertions. Production display composition calls update this surface and publish coalesced complete frames after observable changes. The existing T6963 emulator's memory and framebuffer conversion are reused where they preserve current display semantics, but the virtual target does not replay STM32 GPIO pin toggles merely to reach that model.

Automated tests can inspect frame pixels and a structured snapshot of logical rows/state when available. Acceptance should prefer semantic or structured assertions for menus and reserve exact pixel snapshots for rendering regressions.

Alternatives considered:

- Stream individual LCD bus cycles to the desktop model: rejected because it adds transport volume and tests GPIO sequencing rather than application behavior.
- Publish only text rows: rejected because graphics, separators, cursor details, and future plugin display behavior need a pixel-visible result.

### Preserve the keyboard processing path with an in-process virtual controller

Front-panel controls use the existing case-insensitive semantic key names and send distinct press and release operations. An in-process virtual keyboard controller resolves names using the existing matrix/scancode translation and emits the normal wire bytes into `ef800_keyboard_host`; EF800Application then consumes its normal keys and events. It sends the normal initialized indication at startup and implements only the controller commands needed by EF800Application scenarios, expanding behavior when tests require it.

Disconnect, simulator reset, debug stop, or explicit release-all clears every held key. Interactive mode may implement typematic consistently with the L431 policy; automated tests can disable or deterministically schedule repeat events.

Alternatives considered:

- Inject `ef800_plugin_key_event` or menu commands directly: rejected because that bypasses host keyboard translation and global navigation.
- Run a second native process containing the complete keyboard firmware initially: deferred because an in-process protocol endpoint covers the required behavioral path with simpler deterministic orchestration. A two-process controller can be added if controller scheduling itself becomes a test objective.

### Separate interactive time from deterministic scenario time

Manual runs use native simulator real-time synchronization (`--rt`) so debounce, holds, periodic status, and deadlines are usable by a person. Automated scenarios run without wall-clock synchronization and advance through deterministic events and bounded waits. Test controls operate on simulator-visible state and avoid sleeps tied to host load.

The simulator protocol reports its mode so the front panel and harness do not assume wall-clock behavior during accelerated runs.

### Model peripherals by observable contracts and add them incrementally

Virtual storage uses a disposable or explicitly persisted simulator backing image and never operates on a physical EF800 dump by default. Virtual RTC and battery sources expose fixed/settable values and availability. Virtual serial peers exchange real bytes with production port/plugin transports using deterministic response scripts derived from the existing Elta3 and Fennel1 dummies. VBUS, device availability, delays, errors, and cleanup failures are test controls rather than analog simulations.

The initial target enables only workflows for which all required virtual dependencies are modeled. Unsupported workflows report a clear virtual-device-unavailable state instead of silently succeeding. USB MSC host enumeration, bootloader updates, and physical QSPI qualification remain outside the first virtual platform.

### Treat native plugin failures as controlled scenarios, not MPU evidence

Healthy and adversarial test plugins are compiled for the native target against the same source-level isolated contract. Delay, non-return, stale completion, malformed service requests, and declared failure points exercise production supervision and cleanup. Tests must not cause arbitrary host-process segmentation faults and then claim they represent contained ARM MPU faults; unrecoverable native memory faults may terminate the simulator.

The virtual suite validates that the UI stays schedulable, deadlines fire, outputs are discarded, resources are released, quarantine/navigation are correct, and another plugin remains usable. ADR-0002's MPU permissions and CPU-exception classification remain STM32L496 gates.

## Risks / Trade-offs

- [Hardware adapters remain entangled with EF800Application orchestration] -> Extract the smallest interfaces around currently direct device use and add characterization tests before moving behavior.
- [Virtual and physical behavior drift] -> Share domain/orchestration source, keep adapter contracts narrow, and run the same behavioral scenarios against hardware where feasible.
- [The front-panel protocol becomes an unbounded remote-control API] -> Bind to a developer-configured local endpoint, version the protocol, enforce message and payload bounds, and expose only declared simulation controls.
- [Tkinter networking blocks the GUI or simulator] -> Use non-blocking/background transport handling with bounded queues and perform widget updates on the Tk event loop.
- [Display frame traffic overwhelms interactive operation] -> Coalesce dirty updates, transmit complete sequenced frames at a bounded rate, and always preserve the latest frame.
- [Accelerated simulated time races ahead of an interactive client] -> Require real-time mode for manual sessions and reserve accelerated mode for deterministic harnesses.
- [Native test plugins differ from ARM packages] -> Test source-level behavior and contract validation virtually, retain ARM package build/loader checks and target execution gates separately.
- [Virtual storage is mistaken for fitted QSPI qualification] -> Label simulator artifacts and results explicitly and retain all QSPI/FAT/USB hardware gates in migration status.
- [A native infinite loop starves the cooperative simulation] -> Run the plugin worker at a supervised priority, include preemption/yield behavior in test plugins, and separately test true CPU starvation only where native scheduling can enforce the intended deadline semantics.

## Migration Plan

1. Add characterization tests around current display composition, keyboard host input, menu navigation, and plugin session behavior before extracting boundaries.
2. Establish a minimal virtual target that boots shared EF800Application behavior headlessly with deterministic storage/time/status adapters.
3. Add the virtual display surface and versioned TCP endpoint, then extend the existing Tkinter renderer into a reconnecting LCD front panel.
4. Add semantic press/release/release-all input through the virtual keyboard-controller protocol and add clickable keypad controls.
5. Add headless scenario controls, virtual serial peers, failure injection, and plugin worker responsiveness/timeout/recovery acceptance tests.
6. Add CI build and deterministic scenario coverage, document interactive startup with real-time mode, and update the migration testing boundary.
7. Preserve the physical `ef800` and `ef800_keyboard` targets throughout; if the virtual increment is rolled back, remove only the virtual target/front panel and retain the shared characterization tests and production adapters.

## Open Questions

- Which exact EF800Application sections should become reusable orchestration modules first so the virtual target does not duplicate the current large `main.c` state machine?
- Should the first storage model mount a FAT image through Zephyr's simulated flash or use the existing backend-neutral persistence fake for an earlier UI/plugin milestone? The former gives stronger filesystem coverage; the latter reduces initial integration scope.
- Which complete physical keypad layout and labels should the first front panel present beyond the already documented semantic key-name set?
- Does the plugin-isolation implementation expose a target-neutral supervisor failure injection API, or should virtual-only adversarial plugins return declared failure records for non-timeout scenarios?
