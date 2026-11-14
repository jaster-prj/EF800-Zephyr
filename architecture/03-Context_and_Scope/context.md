# Context and Scope

## Business context

EF800 acquires measurements from external instruments and presents them through
the front-panel keyboard and graphic LCD. Operators configure ports and
workspaces, install compatible packages, and retrieve files. Service and
engineering workflows use the recovery application, USB MTP/MSC personalities,
and the Virtual EF800 browser environment.

## System context

The system context and principal external actors are shown in the
[level-1 block diagram](../diagram_as_code/05_building_block_view/1_level.puml).
The EF800 system contains the L496 EF800 Bootloader, EF800 Application, EF800 Backup,
L431 keyboard controller, internal flash, and external QSPI. It interacts with
operators, serial instruments, a service PC, and (for native-sim) a browser
gateway and developer toolchain.

## External interfaces

| Interface | Producer / consumer | Purpose and boundary |
|---|---|---|
| Main-to-keyboard UART | L496 EF800 Application ↔ L431 keyboard image | Keyboard-ready indication, command/ACK frames, translated key events, and controller update workflow. Normal protocol: 115200 8N1. |
| Measurement serial ports | EF800 Application ↔ assigned instrument | Configured per-port transport. Protocol adapters are selected through the port/plugin assignment model. |
| USB device MTP | EF800 Application or EF800 Backup ↔ PC/service host | Named, filesystem-mediated roots only. Boot NVS and raw flash are not exported. Main exposes Data/Plugins/FWU; Backup also exposes Configuration while it is sole application owner. |
| USB device MSC | Main ↔ PC host | Optional physical composition; raw Shared Data ownership is exclusive and uses a restart/unmount boundary. Excluded in native-sim Main profile. |
| Virtual front-panel TCP | Main native-sim ↔ gateway | Private Compose-network protocol for display, input, bounded control, logs, and diagnostics. Browser does not connect to this socket directly. |
| Browser HTTP/WebSocket | Gateway ↔ browser | Serves UI and mediates approved simulator actions and MTP file operations. It does not directly manipulate simulator filesystem storage for MTP. |
| west / Twister | Developer ↔ repository/container | Build and test integration; not a runtime firmware interface. |

Detailed protocols are documented in [`../../docs/keyboard-protocol.md`](../../docs/keyboard-protocol.md),
[`../../docs/usb-mtp.md`](../../docs/usb-mtp.md), and
[`../../docs/virtual-ef800.md`](../../docs/virtual-ef800.md).
