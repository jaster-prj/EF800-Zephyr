# Solution Strategy

| Strategy | Realization |
|---|---|
| Preserve one EF800 Application composition across physical and virtual targets. | `application/ef800-application/ef800-application-sources.cmake` is the shared source manifest. CMake selects physical or native-sim adapters and overlays. |
| Separate boot policy from the operating application. | The second-level Bootloader validates images, installs eligible Main updates, records boot state, and selects Main or Backup. |
| Keep recovery available without changing normal Main ownership. | Backup mounts Configuration and Shared Data and starts recovery MTP only when the legacy-layout check passes and mounts succeed. |
| Separate persistent concerns by partition. | QSPI boot state uses NVS; settings and profiles use Configuration FAT; workspace/data/plugin/update files use Shared Data FAT. |
| Make storage ownership explicit. | FAT/MSC transitions go through the storage-owner state machine. MTP is a bounded file interface over mounted volumes, not a raw-disk interface. |
| Keep external packages behind a host contract. | PluginManager scans `.PMP` packages; host code owns ports, UI integration, storage, and package lifecycle. Target ABI differs between ARM and native-sim. |
| Use native simulation for repeatable behavioral testing. | Zephyr device emulators and virtual peripherals supply GPIO, UART, I2C, RTC, keyboard, display, battery, storage, and USB-host behavior. |
| Contain failure with explicit defaults and gates. | Missing configuration, invalid profiles, absent optional devices, and unavailable QSPI generally degrade a feature rather than inventing data or silently changing persistent media. |

The corresponding design choices are recorded in
[`../09-Architectural_Decisions/index.md`](../09-Architectural_Decisions/index.md).
