# Use Zephyr Device Emulation for Production Native-Sim Execution

- Status: accepted
- Date: 2026-10-07
- Supersedes: None

## Context

The accepted native-sim architecture uses a dedicated behavioral simulator with virtual adapters, while the current Virtual EF800 application models a simplified UI rather than executing the production MainApplication composition root. The production path already expresses important behavior through Zephyr GPIO, UART, I2C, flash, filesystem, RTC, battery, and device-driver APIs. Replacing those paths with a second application model would reduce the simulator's evidence value and allow behavior to drift from the physical target.

The project does not need STM32 register emulation or exact ARM execution. It needs a coherent native device environment that preserves production source and driver boundaries while providing deterministic, inspectable peripheral behavior.

## Decision

The Virtual EF800 will execute the production MainApplication composition root on Zephyr `native_sim` and will satisfy its physical device dependencies through Zephyr-compatible emulated or simulated devices. GPIO banks, UART links, I2C-connected peripherals, simulated storage, RTC, battery, keyboard, and LCD wiring will be represented at the device/API boundary used by production code.

The default profile will model a healthy device. Availability, delays, register values, scripted serial responses, storage state, and bounded failures will be explicit simulator profile controls. Virtual wiring may connect emulated output state to other virtual models, but it will not claim to reproduce STM32 registers, electrical timing, PCB wiring, or hardware qualification evidence.

## Consequences

- Production startup, driver calls, keyboard processing, LCD access, persistence, workspace, and plugin behavior are exercised together on native-sim.
- Native-sim requires a maintained devicetree, emulator set, and wiring coordinator that mirror the production device names and observable contracts.
- Peripheral emulators must be tested against the actual Zephyr drivers and reject unsupported transactions rather than silently succeeding.
- Physical board targets remain separate and retain all hardware qualification gates.
- The simplified virtual application path must either be retired or retained only for focused virtual-component tests once production native-sim startup reaches feature parity.
