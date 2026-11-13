## Why

The current Virtual EF800 is a lightweight native-sim application that models a separate menu and virtual display, but it does not execute the production `ef800-application` composition root. As a result, native behavioral tests do not exercise the existing GPIO, UART, I2C, LCD, keyboard, storage, RTC, battery, persistence, and startup paths together. This change will provide a healthy-by-default native-sim device environment so the production application can run without EF800 hardware while retaining explicit controls for peripheral failure scenarios.

## What Changes

- Add a native-sim build of the production `ef800-application` source set with devicetree nodes and configuration matching the device names expected by the existing application.
- Provide emulated GPIO controllers for the physical STM32 GPIO banks and connect their output/input transitions to virtual EF800 devices.
- Provide emulated UART devices for the keyboard link and measurement ports, with a host-visible native console where appropriate.
- Provide an emulated I2C controller and device models for the MCP23017, DS3231, and MAX17048/MAX17043-compatible peripherals, reusing existing Zephyr emulators where suitable.
- Route MCP23017 output state into keyboard reset, LCD control, USB-select, and related virtual peripheral behavior.
- Drive the existing T6963 GPIO driver through emulated bus activity and publish its resulting 240 x 64 framebuffer to the existing Virtual EF800 front-panel transport.
- Mount a disposable simulated-flash FAT volume by default so persistence, workspace, metadata, and plugin startup paths can execute normally.
- Define a healthy default peripheral profile and explicit controls for unavailable devices, delays, malformed responses, and bounded operation failures.
- Preserve direct headless operation, browser deployment compatibility, and the boundary that native-sim results do not qualify STM32 hardware, electrical behavior, or ARM-specific protection.
- Add startup, device-readiness, wiring, healthy-profile, and failure-injection acceptance coverage.

## Capabilities

### New Capabilities

- `native-sim-ef800-peripherals`: Device-level emulation and virtual wiring required to run the production EF800 application on native-sim.

### Modified Capabilities

- `virtual-ef800-platform`: The Virtual EF800 shall execute the production application path through emulated peripheral devices rather than only a simulator-specific UI model, while retaining interactive/headless operation and the hardware qualification boundary.

## Impact

- Affected targets include `application/ef800-virtual`, `application/ef800-application`, native-sim devicetree/Kconfig configuration, shared EF800 drivers, virtual peripheral components, and native test coverage.
- The change adds native-sim device and bus emulator code, virtual wiring callbacks, deterministic peripheral profiles, and simulated storage setup.
- Existing physical `ef800` and `ef800_keyboard` targets remain unchanged and continue to use their hardware devicetrees and drivers.
- Existing front-panel and web gateway protocols remain the observation and input boundary; no browser-facing protocol replacement is required.
