# Use a Single EF800 MainApplication Target for Physical and Native-Sim Builds

- Status: accepted
- Date: 2026-10-07
- Supersedes: None

## Context

The Virtual EF800 currently has a separate application wrapper even though it already compiles the production `ef800-main` entry point and most of the same shared sources. Maintaining two application CMake source lists creates a structural opportunity for physical firmware and native-sim behavior to drift.

The physical and native-sim targets must use different devicetrees and platform adapters, but they should exercise the same MainApplication source composition.

## Decision

`application/ef800-main` will be the single MainApplication target for both the physical `ef800` board and Zephyr `native_sim`. A shared production source manifest will be used by both builds. Board-specific Kconfig, devicetree overlays, and adapter source lists will select physical STM32/QSPI/USB/watchdog behavior or native-sim emulated GPIO/UART/I2C/storage/peripheral behavior.

The separate `application/ef800-virtual` application wrapper and any duplicate simulator-only application model will be removed. Virtual peripheral emulators, front-panel protocol, browser gateway, and native-sim adapter modules remain supported infrastructure.

## Consequences

- Changes to MainApplication source composition are automatically visible to both physical and native-sim builds.
- The unified CMake target must explicitly avoid applying STM32 overlays to native_sim.
- Target-specific adapters and Kconfig remain necessary; source parity does not imply hardware parity.
- Existing build and Docker commands using `application/ef800-virtual` must migrate to `application/ef800-main` with `-b native_sim`.
- Physical image layout and hardware qualification gates remain unchanged.
