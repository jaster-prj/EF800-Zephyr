## Context

The repository already has a `native_sim` Virtual EF800, a web gateway, a T6963 behavioral model, and several virtual application helpers. The current virtual application owns a simplified menu loop and writes directly to a virtual framebuffer; it does not execute the production `application/ef800-main/src/main.c` composition root. The production path expects named GPIO, UART, I2C, flash, filesystem, RTC, battery, keyboard, LCD, persistence, and port devices from its STM32 devicetree.

Zephyr v4.4.1 provides the needed native facilities: `zephyr,gpio-emul`, `zephyr,uart-emul`, `zephyr,i2c-emul-controller`, simulated flash, and the emulator registration framework. Zephyr also contains a MAX17048 emulator that can be reused or adapted for the EF800 raw MAX17043-compatible register access. The accepted native-sim and web-gateway ADRs remain in force: this is behavioral source-level evidence, not STM32 register, ARM MPU, electrical, or physical-peripheral qualification, and browser presentation remains outside the Zephyr application.

## Goals / Non-Goals

**Goals:**

- Run the production EF800 MainApplication composition root on `native_sim`.
- Preserve existing Zephyr GPIO, UART, I2C, flash, filesystem, RTC, and device-driver API paths.
- Provide healthy-by-default emulated MCP23017, DS3231, MAX17048/MAX17043-compatible, keyboard, LCD, and serial-peer behavior.
- Connect emulated GPIO transitions into a virtual T6963 bus model and other observable EF800 wiring.
- Mount disposable simulated FAT storage by default while permitting explicitly selected persistent simulator storage.
- Expose deterministic profile controls for availability, delays, values, scripted responses, and bounded failures.
- Keep physical board targets and browser/front-panel transport contracts intact.

**Non-Goals:**

- Emulating STM32L496/L431 registers, clocks, exact instruction execution, or electrical signal timing.
- Running the production ARM image unchanged on native_sim.
- Qualifying QSPI geometry, USB behavior, bootloader handoff, MPU permissions, ARM faults, or fitted PCB wiring.
- Implementing every T6963 command or every USB/MSC hardware behavior when not needed by enabled MainApplication workflows.
- Replacing the web gateway with a Zephyr HTTP implementation.

## Decisions

### Use a native-sim composition that reuses the production source set

The virtual target will include the production MainApplication sources and shared services rather than extending the current simplified virtual menu. Its native-sim devicetree and Kconfig will satisfy the node labels, chosen nodes, and feature symbols expected by the production code.

This is preferred over a second application model because startup, keyboard parsing, persistence, workspace, plugin supervision, and menu behavior then remain production code. Physical `ef800` configuration remains separate, preventing native-only devices from entering firmware builds.

### Map physical GPIO banks to emulated controllers

The native overlay will expose logical GPIO controllers corresponding to the production `gpioa`, `gpiob`, `gpioc`, and `gpiod` references, backed by Zephyr GPIO emulation. The virtual wiring layer will register callbacks and inspect output state or inject input state. Interrupt capabilities required by the LCD, reset, and input paths will be enabled explicitly.

One controller per logical bank is preferred over a single aggregate controller because it preserves the physical port/pin vocabulary and makes devicetree diagnostics easier. The emulated controllers model GPIO API behavior, not STM32 register semantics.

### Use emulated UARTs for internal peers and a native PTY for the console

The native overlay will provide the production node labels `lpuart1`, `usart1`, `usart3`, and `uart4`. The keyboard and measurement links will use `zephyr,uart-emul`, allowing deterministic in-process injection and TX observation. The debug console may use a native PTY UART so developers can inspect logs and shell behavior.

The virtual keyboard controller will translate semantic press/release events into the existing keyboard protocol bytes and inject them through the emulated `lpuart1` RX path. It will emit the normal startup response when the emulated MCP23017 keyboard-reset output is released.

### Use an emulated I2C controller with device-level register models

The virtual I2C bus will use `zephyr,i2c-emul-controller`. The existing Zephyr MCP23017 GPIO driver will remain in the path, with a custom MCP23017 I2C emulator implementing the register operations needed by that driver. The existing EF800 MCP helper will therefore continue to use the normal GPIO-controller API.

The DS3231 emulator will implement the register-level RTC contract used by the Zephyr DS3231 RTC driver, including BCD time values and read/write support. Its initial time will be deterministic and its ticking policy will be configurable. The Zephyr MAX17048 emulator will be reused or adapted to answer the EF800 raw VCELL, SOC, and VERSION reads with configurable healthy values.

This bus-level approach is preferred over replacing the EF800 driver helpers because it exercises the same driver/API chain as the physical target while still allowing controlled virtual state.

### Route MCP23017 state into virtual wiring

The MCP23017 model will publish output-latch changes to a wiring coordinator. The coordinator will drive:

```text
MCP pin 8  --> USB-select state
MCP pin 12 --> keyboard reset state
MCP pin 4  --> LCD FS state
MCP pin 5  --> LCD reset state
```

The coordinator will not emulate USB electrical behavior. It will expose observable device state and feed only the virtual keyboard and LCD models required by MainApplication workflows.

### Detect T6963 transactions from emulated GPIO activity

The production T6963 driver will remain unchanged. A virtual T6963 bus observer will track the emulated data pins and CD, WR, RD, CE, FS, and reset signals. It will interpret complete write/read cycles and delegate command/data behavior to the existing T6963 behavioral model or an equivalent shared model.

Read cycles will update the emulated data input pins before the production driver samples them. Write cycles will consume the data bus when the virtual chip-select and write edges indicate a complete operation. The model will report ready status by default and publish complete framebuffer snapshots to the existing virtual front-panel protocol.

### Use real simulated storage with disposable defaults

The native target will configure simulated flash and the Zephyr flash-disk/FAT path needed by `ef800_storage_mount()`. The default run will use an isolated disposable backing store. A persistent backing path will be an explicit simulator option, not an implicit reuse of any hardware image or host workspace state.

This allows the production persistence, workspace, metadata, and plugin discovery paths to execute while preserving test isolation.

### Keep healthy behavior separate from fault injection

A virtual peripheral profile will hold availability, values, delays, scripts, and failure modes. The default profile will make all required devices healthy. Tests and headless clients will change the profile through a bounded simulator control surface; production workflow code will not contain simulator conditionals.

The profile will reset between runs unless persistent state is explicitly requested. Device errors will be bounded and observable, avoiding host crashes as substitutes for hardware failures.

### Extend the internal simulator protocol for profile controls

The existing bounded binary simulator protocol will gain profile-control and profile-state message types. A control payload will contain a bounded device identifier, operation, and integer/byte parameters. The native simulator will validate lengths, identifiers, and ranges before changing profile state and will return an acknowledgement or bounded error state.

The web gateway will continue to expose only browser display and keypad operations. Profile controls remain available to direct headless scenario clients and native test harnesses, preventing arbitrary browser sessions from changing device faults.

## Risks / Trade-offs

- [The large production composition root depends on additional STM32-specific services] -> Start with a native-sim compile and startup inventory, preserve explicit unsupported-device diagnostics, and add emulation only for dependencies reached by the healthy boot path.
- [GPIO edge observation may misidentify bus cycles] -> Define cycle recognition around CE/WR/RD transitions, add unit tests for valid, incomplete, and malformed cycles, and keep the existing T6963 model as the state oracle.
- [MCP23017 register behavior differs from the Zephyr driver expectations] -> Test the emulator against the actual Zephyr MCP23017 driver and assert configuration, output latch, and readback behavior.
- [The DS3231 or MAX17048 driver changes its transaction shape] -> Keep wire-level I2C fixtures and reject unsupported message layouts explicitly rather than silently returning success.
- [Native-sim storage state leaks between tests] -> Use disposable per-run backing by default and make persistent storage opt-in with an explicit path.
- [Internal emulated UART peers hide concurrency defects] -> Preserve UART interrupt-driven operation, use the emulator work queue, and test delayed/interleaved RX and TX behavior.
- [Virtual success is mistaken for hardware qualification] -> Keep simulator logs, documentation, and migration status explicit about the hardware gates that remain open.
- [The existing web deployment expects the old simplified simulator state] -> Preserve the bounded front-panel protocol and add compatibility tests around retained display state, keyboard events, and headless startup.

## Migration Plan

1. Add the native-sim application composition and devicetree node labels without changing physical board targets.
2. Configure emulated GPIO banks, UARTs, I2C controller, simulated flash, and native-sim Kconfig; build far enough to identify missing production dependencies.
3. Add the MCP23017, DS3231, and MAX17048/MAX17043-compatible device models and verify the healthy foundation initialization path.
4. Add keyboard UART behavior and MCP-controlled keyboard reset wiring; verify the existing keyboard host reaches its normal ready state.
5. Add T6963 GPIO-cycle observation and connect the production LCD driver to the existing virtual framebuffer/front-panel publication path.
6. Enable FAT storage and verify persistence, workspace, metadata, and plugin initialization with disposable state.
7. Add peripheral profile controls and healthy/degraded/failure acceptance tests.
8. Update virtual EF800 documentation and migration-status evidence, then validate that the browser gateway and direct headless clients remain compatible.

Rollback consists of selecting the existing simplified virtual target or removing only the native-sim production composition and emulator sources; physical firmware targets, shared hardware drivers, and the web gateway remain independently usable.

## Open Questions

- Whether the existing Zephyr DS3231 implementation needs a custom emulator or can be paired with an available test-only model without adding duplicate RTC logic.
- Whether the production USB/MCUmgr code should be disabled in the first healthy profile while preserving the virtual VBUS and ownership state, or whether a native-sim USB model is required immediately.
