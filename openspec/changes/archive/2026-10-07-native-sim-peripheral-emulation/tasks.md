## 1. Native-sim production composition

- [x] 1.1 Add a native-sim application composition that reuses the production `ef800-application` source set and shared services without changing the physical EF800 target.
- [x] 1.2 Add native-sim Kconfig enabling the production GPIO, interrupt-driven UART, runtime UART configuration, I2C, flash, filesystem, FAT, NVS, RTC, logging, and required plugin/application services.
- [x] 1.3 Add a native-sim devicetree overlay exposing the production node labels `gpioa`, `gpiob`, `gpioc`, `gpiod`, `lpuart1`, `usart1`, `usart3`, `uart4`, and the required chosen/user properties.
- [x] 1.4 Build the production composition on native-sim and record any remaining unavailable startup dependencies before adding device models.

## 2. GPIO and UART emulation

- [x] 2.1 Configure one Zephyr GPIO emulator per logical EF800 GPIO bank with the interrupt capabilities required by the production paths.
- [x] 2.2 Add emulated UART devices for the keyboard and measurement links while retaining a native PTY or equivalent host-visible path for the debug console.
- [x] 2.3 Implement the virtual keyboard controller that injects the existing keyboard protocol through the emulated `lpuart1` device and observes TX commands.
- [x] 2.4 Implement scripted measurement UART peers with configurable response bytes, delays, unavailable state, and bounded errors.
- [ ] 2.5 Add unit tests for GPIO input injection, output observation, UART interrupt behavior, delayed RX, and interleaved TX/RX.

## 3. I2C peripheral models

- [x] 3.1 Configure the native-sim `zephyr,i2c-emul-controller` and attach device nodes at the MCP23017, DS3231, and MAX17048-compatible addresses.
- [x] 3.2 Implement the MCP23017 I2C register emulator for direction, input, GPIO, and output-latch operations used by the Zephyr GPIO driver.
- [x] 3.3 Verify the MCP23017 emulator through the existing EF800 GPIO helper and assert configuration, output changes, and readback behavior.
- [x] 3.4 Reuse or adapt the Zephyr MAX17048 emulator for the EF800 raw VERSION, SOC, and VCELL register transactions and configurable healthy values.
- [x] 3.5 Implement the DS3231 register emulator with deterministic BCD time, read/write support, and configurable ticking or unavailable behavior.
- [ ] 3.6 Add wire-level I2C tests for valid transactions, unsupported message layouts, register values, and bounded device failures.

## 4. Virtual wiring and LCD path

- [x] 4.1 Add a virtual wiring coordinator that observes MCP23017 output-latch changes and exposes keyboard-reset, LCD-reset, LCD-FS, and USB-select state.
- [x] 4.2 Connect keyboard-reset transitions to the virtual keyboard controller's startup response behavior.
- [x] 4.3 Add a T6963 GPIO-bus observer for data pins, CD, WR, RD, CE, FS, and reset signals.
- [x] 4.4 Route complete emulated T6963 write and read cycles to the existing T6963 behavioral model and drive read data/status back onto emulated GPIO inputs.
- [x] 4.5 Publish framebuffer changes from the production T6963 path through the existing Virtual EF800 display/front-panel protocol.
- [ ] 4.6 Add tests for valid LCD command/data cycles, readback, reset, incomplete cycles, and malformed control transitions.

## 5. Simulated storage and healthy profile

- [x] 5.1 Configure native-sim simulated flash and flash-disk/FAT mounting for the EF800 storage name and mount point expected by production code.
- [x] 5.2 Make disposable storage the default and add an explicit opt-in persistent backing configuration without accepting physical EF800 images implicitly.
- [x] 5.3 Add the healthy peripheral profile with operational GPIO, UART, I2C, RTC, battery, keyboard, LCD, storage, and serial-peer behavior.
- [x] 5.4 Add profile state and control handling for availability, delays, register values, scripted responses, storage state, and bounded failures.
- [x] 5.5 Verify production foundation initialization reports ready MCP23017, RTC, battery, storage, keyboard, and LCD behavior on the healthy profile.

## 6. Production workflow and front-panel integration

- [x] 6.1 Verify persistence, workspace, metadata, plugin registration, port assignment, and EF800 Application initialization execute against disposable virtual storage.
- [x] 6.2 Verify semantic front-panel press, release, and release-all events traverse the emulated keyboard UART and existing keyboard/application handlers.
- [x] 6.3 Verify browser gateway and direct headless clients receive the production-generated LCD state and remain compatible with the bounded simulator protocol.
- [x] 6.4 Add native-sim startup and healthy-profile acceptance tests that run without a connected front panel.
- [ ] 6.5 Add degraded-startup and failure-injection acceptance tests for unavailable storage, RTC, battery, serial peer, keyboard, and LCD devices.

## 7. Documentation and qualification boundary

- [x] 7.1 Update Virtual EF800 operating documentation with the production composition, healthy profile, disposable storage, and explicit fault controls.
- [x] 7.2 Document which emulator paths provide behavioral evidence and which STM32, ARM, USB, QSPI, electrical, and wiring gates remain hardware-only.
- [x] 7.3 Run the required native tests and Virtual EF800 startup/build checks in the reusable devcontainer.
- [x] 7.4 Update migration status with the completed native-sim production-peripheral coverage and remaining qualification gaps.
