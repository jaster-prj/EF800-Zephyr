## Context

The Virtual EF800 currently has its own `application/ef800-virtual` CMake target, but that target already compiles `application/ef800-application/src/main.c` and most of the same shared sources. The two CMake files manually maintain parallel source lists. The physical target also has an STM32-specific `app.overlay` and physical-only adapters, while native_sim needs emulated GPIO/UART/I2C/storage devices and native watchdog/USB substitutions.

The desired architecture is one production application target with two board configurations. Existing accepted decisions require native_sim behavioral execution, preserve the hardware qualification boundary, and keep the browser gateway outside the Zephyr application. The external PluginManager remains a shared build dependency.

## Goals / Non-Goals

**Goals:**

- Build `application/ef800-application` for both `ef800` and `native_sim`.
- Maintain one shared production source manifest for EF800 Application and shared services.
- Select physical or native-sim adapters explicitly by board/configuration.
- Retain native peripheral emulators, profile controls, T6963 wiring, keyboard bridge, and browser/front-panel infrastructure.
- Remove the duplicate virtual application wrapper and obsolete simplified virtual `main.c`.
- Preserve physical image layout, hardware behavior, and release gates.

**Non-Goals:**

- Making physical and native devicetrees identical.
- Removing virtual hardware adapters or the browser gateway.
- Adding STM32 register emulation or changing hardware qualification claims.
- Refactoring every target-specific branch in one increment when a narrow adapter boundary is sufficient.

## Decisions

### Use `application/ef800-application` as the only EF800 Application target

The physical and native builds will use the same application directory and entry point. The old `application/ef800-virtual` wrapper will be removed after its retained infrastructure sources are moved or referenced from the shared target.

Alternatives considered:

- Keep two application wrappers: rejected because source lists and startup behavior can drift.
- Move EF800 Application into a new common application: rejected because it creates another migration path and disrupts the existing physical target.

### Define one shared source manifest and per-target adapter lists

The CMake configuration will separate:

```text
common production sources
    +--> physical adapters when BOARD=ef800
    +--> native-sim adapters when BOARD=native_sim
```

The common list includes `main.c`, keyboard host, plugin host, persistence, measurement, ports, plugins, UI, and shared drivers. Physical-only files include the STM32 watchdog and hardware USB MSC adapter. Native-only files include the native watchdog, USB stub, emulators, GPIO bus observer, profile, and front-panel bridge.

This makes source parity visible and reviewable while preserving target-specific device behavior.

### Select overlays and Kconfig by board

The unified CMake target will select the physical STM32 overlay/configuration for `ef800` and the native-sim overlay/configuration for `native_sim`. The native overlay will provide the same logical node labels expected by EF800 Application, while the physical overlay retains STM32 pinctrl, clocks, partitions, USB, and QSPI definitions.

Common Kconfig remains in `prj.conf`; board-specific options move to board-specific configuration files where necessary. Native-only FAT formatting/profile options will not enter physical firmware.

### Keep virtual infrastructure, remove only duplicate application behavior

The following remain valid native-sim infrastructure:

```text
GPIO/UART/I2C/device emulators
T6963 bus observer and model
MCP/RTC/battery/profile services
virtual display/protocol/front-panel modules
browser gateway and Compose runtime
```

Only the old simulator-specific menu/application entry point and its wrapper target are removed. The front panel continues to observe the production T6963 path.

### Preserve deployment compatibility through path updates

Dockerfiles, Compose, CI, and documentation will build `application/application/ef800-application` with `-b native_sim` and copy the resulting production native-sim executable. The browser gateway protocol and runtime port remain unchanged.

## Risks / Trade-offs

- [Physical app overlay is accidentally applied to native_sim] -> Set explicit board-dependent overlay selection and add clean builds for both boards.
- [Source manifest migration drops a file] -> Compare physical and native link source lists and require both builds to pass before removing the wrapper.
- [Native-only code leaks into the physical firmware] -> Keep adapters in target-specific CMake branches and use board-specific Kconfig/overlay files.
- [Compose or Docker paths retain the deleted virtual application path] -> Update every build, Dockerfile, CI, and documentation reference before removal.
- [Shared code remains coupled to `CONFIG_BOARD_NATIVE_SIM`] -> Keep existing behavior for this increment and identify future adapter extractions as separate work rather than broadening scope.

## Migration Plan

1. Extract the common production source list from the two CMake targets.
2. Add board-dependent adapter selection to `application/ef800-application/CMakeLists.txt`.
3. Move or select native-sim overlay and Kconfig files without changing physical target behavior.
4. Build physical `ef800` and native_sim from the unified target and compare generated source/device configuration.
5. Update Docker Compose, Dockerfiles, CI, docs, and test commands to use the unified target.
6. Remove `application/ef800-virtual` and the obsolete simplified virtual entry point.
7. Run native tests, physical firmware builds, image verification, and native-sim startup checks.

Rollback restores the virtual wrapper target and its previous Docker/build paths while leaving the physical target and shared emulators intact.

## Open Questions

- Whether the physical `app.overlay` should be renamed to a board-specific overlay or selected explicitly through CMake while preserving the current file for compatibility.
- Whether a future change should eliminate the remaining `CONFIG_BOARD_NATIVE_SIM` branches from shared production services in favor of a formal platform adapter API.
