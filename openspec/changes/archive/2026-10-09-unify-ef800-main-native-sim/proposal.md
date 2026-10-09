## Why

The production EF800 firmware and Virtual EF800 currently use separate application wrappers and manually maintained source lists. Although the Virtual EF800 now compiles the production `ef800-main` sources, its separate `application/ef800-virtual` target can drift from the physical firmware target and leaves an obsolete simulator-specific application model in the repository.

The project should make one `application/ef800-main` application buildable for both the physical `ef800` board and Zephyr `native_sim`, with only devicetree, Kconfig, and platform-adapter differences between targets.

## What Changes

- **BREAKING** Remove the `application/ef800-virtual` application wrapper and its obsolete standalone virtual `main.c` path.
- Make `application/ef800-main` buildable for both `-b ef800` and `-b native_sim`.
- Establish one shared production source manifest for both targets so MainApplication, keyboard, persistence, measurement, ports, plugins, and UI sources cannot drift.
- Move physical and native-sim differences into explicit target-specific Kconfig, devicetree overlays, and adapter source lists.
- Preserve the native-sim emulated GPIO, UART, I2C, RTC, battery, storage, T6963, keyboard, profile, and front-panel infrastructure that is still required by the Virtual EF800.
- Remove only duplicate virtual application-model implementations; retain virtual hardware emulators and front-panel protocol components.
- Update native build, Docker/Compose, documentation, and test commands to build `application/application/ef800-main` for `native_sim`.

## Capabilities

### New Capabilities

<!-- None. This change consolidates an existing target architecture. -->

### Modified Capabilities

- `virtual-ef800-platform`: The behavioral simulator SHALL use the same `ef800-main` application target and production source composition as the physical firmware while selecting native-sim peripheral adapters at build time.
- `native-sim-ef800-peripherals`: Native-sim peripheral emulation SHALL remain available when the shared `ef800-main` target is built for `native_sim`, without relying on a separate virtual application wrapper.

## Impact

- Affected build files include `application/ef800-main/CMakeLists.txt`, Kconfig/configuration files, physical and native-sim overlays, Dockerfiles, Compose documentation, and CI/test commands.
- `application/ef800-virtual` is removed or retired as an application target; its reusable emulation and front-panel components remain under shared/application support directories.
- Physical `ef800` firmware behavior and image layout must remain unchanged.
- Native-sim builds continue to use the external `PluginManager` module and the existing production plugin-host path.
