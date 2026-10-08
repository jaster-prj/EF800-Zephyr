## Why

MainApplication behavior, including plugin concurrency, menu interaction, display output, persistence, and failure recovery, currently requires either narrow native tests or physical EF800 hardware. A behavioral EF800 target on `native_sim` would provide an interactive and scriptable product-level environment without first emulating the STM32L496, STM32L431, or PCB electronics.

## What Changes

- Add a virtual EF800 application target that runs production application, plugin, and thread behavior on Zephyr `native_sim` while replacing physical peripherals with deterministic virtual devices.
- Provide a live 240 x 64 virtual LCD using the EF800 display semantics and a host-visible framebuffer.
- Provide a virtual keypad that sends distinct semantic key press and release events through the normal MainApplication keyboard-processing path.
- Support an interactive real-time mode for manual use and a deterministic scriptable mode for automated behavioral and plugin-thread tests.
- Provide virtual storage, RTC, battery, serial-device, and controllable failure behaviors needed by MainApplication workflows, introduced incrementally according to test coverage.
- Reuse the existing T6963 emulator/viewer and keyboard naming/protocol concepts where practical rather than introducing incompatible display or input semantics.
- Keep STM32 register emulation, ARM binary execution, MPU/fault qualification, electrical behavior, and physical-device qualification out of scope.

## Capabilities

### New Capabilities

- `virtual-ef800-platform`: Defines the interactive and automated `native_sim` EF800 environment, virtual LCD and keypad behavior, host transport, deterministic peripheral models, and the boundary between behavioral simulation and hardware qualification.

### Modified Capabilities

None.

## Impact

- A new virtual EF800 application/build target and native-simulator configuration.
- MainApplication integration boundaries for display, keyboard input, storage, time, battery status, serial peers, and fault injection where current code binds directly to EF800 devicetree devices.
- Existing shared T6963, keyboard protocol, plugin session, persistence, and port behavior reused by the virtual target.
- A host-side virtual front panel, likely extending the existing Tkinter `GSE/TLX-711A/virtual_display.py` renderer, with a bidirectional transport suitable for Windows-to-container use.
- Native behavioral tests and CI scenarios for menu navigation, plugin worker responsiveness, timeout cleanup, quarantine, and output/workspace integrity.
- Documentation distinguishing virtual behavioral evidence from STM32 MPU, boot-chain, electrical, QSPI, USB, and physical peripheral evidence.
