# Use native_sim for Behavioral EF800 Simulation

- Status: accepted
- Date: 2026-10-06
- Supersedes: None

## Context

EF800 development needs product-level tests that combine EF800 Application navigation, plugin lifecycle, Zephyr threads, display state, keyboard input, persistence, and serial peers without requiring physical hardware. Running the production ARM image would require emulating STM32L496 and STM32L431 peripheral registers and the connected PCB devices, which is disproportionate to the desired behavioral coverage.

The project already uses Zephyr `native_sim` for shared tests and has reusable display, keyboard, persistence, plugin, and serial-device components. ADR-0002 separately requires STM32L496 evidence for MPU permissions and CPU-fault containment; a host-native simulator cannot provide that evidence.

## Decision

EF800 product-level behavioral simulation will use a dedicated Zephyr `native_sim` application that shares production domain and orchestration code and substitutes explicit virtual adapters for physical display, keyboard, storage, status, and communication devices. Interactive and automated clients will use the same bounded simulation interface.

Virtual-platform results may qualify covered application control flow, concurrency, plugin supervision, cleanup, navigation, and data-integrity behavior. They will not qualify STM32 register behavior, ARM executable loading, MPU permissions, CPU exception classification, boot handoff, electrical behavior, or physical peripheral operation. Those remain separate target and hardware gates.

## Consequences

- Product workflows and plugin-thread behavior can run interactively and in CI without EF800 hardware.
- EF800 Application hardware boundaries must be narrow enough for physical and virtual adapters to share production behavior without forking the application model.
- Native test plugins validate source-level contracts and supervision but do not replace ARM package-loader or MPU tests.
- QEMU or Renode MCU emulation is not required for the behavioral simulator and may be pursued independently if exact-binary execution becomes valuable.
- Documentation and test reports must identify virtual evidence and retain all applicable physical release gates.
