# Proposal

## Why

EF800Application already consumes most of the STM32L496's internal SRAM, while the EF800 board has soldered 512 KiB asynchronous SRAM connected to FMC. The firmware needs to use that memory predictably for application-owned allocations and provide a scalable, isolated memory budget for heavier plugins, while retaining core operation with reduced plugin support if external SRAM is unavailable.

## What Changes

- Bring up and validate the board's FMC-connected SRAM first in the standalone `test-hwdebug` application, using STM32L496 and IS61 datasheet-derived controller timings and the board schematic as the wiring source of truth.
- Integrate external SRAM as a fixed EF800 board/build memory resource, not a COMPOSITION-selected feature; make EF800Application's memory use of it explicit in the build and link/runtime layout.
- Move suitable EF800Application-owned allocations to external SRAM where beneficial, keeping host hardware/service memory under EF800Application ownership.
- Allow plugin packages to declare bounded plugin-owned memory needs and admit or reject each package against available memory, linker/MPU constraints, and the active runtime profile.
- Keep EF800Application core behavior available when external SRAM validation fails, using internal memory and reduced plugin support rather than relying on external SRAM for boot-critical state.
- Verify external-memory access, MPU permissions, plugin execution/recovery, memory accounting, and fallback on STM32L496 hardware before relying on it in EF800Application.

## Capabilities

### New Capabilities
- `external-sram-memory`: FMC initialization, validation, EF800Application memory allocation, and internal-only fallback behavior for the EF800 external SRAM.

### Modified Capabilities
- `plugin-execution-isolation`: Plugins declare bounded plugin-owned memory requirements; the host validates fit and preserves MPU isolation when using external SRAM or the internal-only fallback.

## Impact

- `boards/ef800` DeviceTree/board configuration and memory/linker integration.
- `application/test-hwdebug` for the initial SRAM test and `application/ef800-application` for runtime initialization, memory allocation, plugin admission, and fallback.
- Plugin package metadata/loader ABI and `openspec/specs/plugin-execution-isolation`.
- Target verification on STM32L496; external SRAM and internal SRAM must both retain the required protection properties.
