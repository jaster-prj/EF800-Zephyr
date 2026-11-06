# Building Block View

## Level 1: EF800 firmware and platform blocks

Level-1 context diagram source: [`1_level.puml`](../diagram_as_code/05_building_block_view/1_level.puml).

The firmware system comprises the second-level Bootloader, MainApplication,
BackupApplication, keyboard resident bootloader, and keyboard application.
Shared EF800 domain modules are reused by the applicable images. Zephyr and its
drivers provide the runtime; pinned PluginManager and ZephyrMTP modules provide
external package and MTP integration. Native-sim adapters supply virtual
peripherals and the virtual USB peer.

## Level 2: MainApplication components

MainApplication component diagram source: [`2_main_components.puml`](../diagram_as_code/05_building_block_view/2_main_components.puml).

`application/ef800-main/src/main.c` is the composition root. Domain behavior is
implemented in shared modules; it wires services to Zephyr APIs and platform
adapters.

| Block | Responsibility | Does not own |
|---|---|---|
| Main composition root | Startup order, service construction, UI state machine, periodic runtime loop, keyboard and USB coordination. | Driver internals, record formats, USB file-root policy. |
| UI and display | Menus, navigation, display formatting, interaction between instrument workflows and LCD. | Measurement transport parsing or filesystem ownership. |
| Keyboard host/protocol | UART command framing, key event buffering/translation, controller update protocol. | Physical key matrix scanning; that belongs to L431. |
| Ports and assignment services | Persistent port-to-device/plugin assignment and transport claiming. | Instrument-specific measurement semantics. |
| Measurement/workspace | Instrument protocol lifecycle, validation and formatting of measurements, workspace persistence. | Dynamic package loading mechanism. |
| Plugin host/registry | Package discovery/activation and contract adaptation; exposes host services through bounded APIs. | Direct ownership of peripherals, filesystems, or host UI state. |
| Persistence/composition | Versioned settings and two-slot composition records, supported legacy settings migration, profile resolution. | Arbitrary FAT formatting or boot decision. |
| Storage/layout/ownership | QSPI legacy signature check, mount/unmount state, partition-aware storage handles, FAT/MSC transition policy. | Physical qualification of flash or host USB behavior. |
| USB manager | Runtime personality choice and MTP/MSC/MCUmgr lifecycle. | Raw-sector access through MTP. |
| Native-sim adapters | Emulated devices and virtual communication endpoints for behavioral tests. | Claims of physical hardware equivalence. |

## Other firmware blocks

| Firmware image | Main building blocks | Key role |
|---|---|---|
| Second-level Bootloader | Reset/watchdog handling, QSPI update scanner, image/vector validation, boot decision, metadata, handoff. | Installs a validated Main update and selects Main or Backup. |
| BackupApplication | Console/watchdog, layout guard, Configuration/Data mounts, composition-backed LCD contrast, recovery MTP. | Recovery file access while Main is unavailable; does not format legacy media. |
| Keyboard resident bootloader | Relocated image validation/update, retained handoff state, IWDG recovery. | Starts the keyboard app or enters factory ROM recovery under defined failures. |
| Keyboard application | Matrix scan, key naming/scancodes, UART protocol, startup health signal, watchdog inheritance. | Supplies physical keypad events to Main; does not share L496 storage. |
