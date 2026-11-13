## 1. Shared production source composition

- [x] 1.1 Extract the common EF800Application and shared-service source list into a reusable CMake manifest.
- [x] 1.2 Update `application/ef800-application/CMakeLists.txt` to consume the common manifest without changing the physical source composition.
- [x] 1.3 Add explicit physical and native-sim adapter source selections for watchdog, USB MSC, emulated devices, wiring, and front-panel support.
- [x] 1.4 Add a source-parity check or build-time diagnostic that makes physical/native source-list drift visible.

## 2. Board-specific configuration

- [x] 2.1 Make `application/ef800-application` select the physical STM32 overlay for `ef800` and the native-sim overlay for `native_sim` before Zephyr devicetree processing.
- [x] 2.2 Split common, physical, and native-sim Kconfig settings so native-only emulation and FAT-format options cannot enter physical firmware.
- [x] 2.3 Preserve the physical image partitions, USB/QSPI configuration, watchdog behavior, and existing `ef800` build output.
- [x] 2.4 Verify the unified target produces both physical and native-sim generated devicetrees with the expected node labels and adapters.

## 3. Remove duplicate virtual application wrapper

- [x] 3.1 Migrate Dockerfiles, Compose build paths, CI, scripts, and documentation from `application/ef800-virtual` to `application/ef800-application -b native_sim`.
- [x] 3.2 Remove the obsolete `application/ef800-virtual` application CMake/Kconfig/entry-point wrapper after all consumers migrate.
- [x] 3.3 Retain and re-home only virtual peripheral, front-panel, protocol, and test infrastructure needed by the unified native-sim target.
- [x] 3.4 Remove stale simplified-menu references and verify no build or documentation path still treats it as the Virtual EF800 application.

## 4. Build and runtime verification

- [x] 4.1 Build `application/ef800-application` for `native_sim` with the production EF800Application and healthy emulated peripherals.
- [x] 4.2 Run the native-sim startup and front-panel smoke checks, including FAT storage, RTC, MCP23017, T6963, battery, keyboard, and profile initialization.
- [x] 4.3 Build `application/ef800-application` for the physical `ef800` board and verify the fixed image contract remains unchanged.
- [x] 4.4 Run the native Twister suite and Virtual EF800 deployment build using the unified target.
- [x] 4.5 Update migration status with source-parity evidence and remaining behavioral-versus-hardware qualification boundaries.
