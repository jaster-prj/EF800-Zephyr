# Deployment View

## Physical EF800

The physical deployment contains an STM32L496 main/debug controller and a
separate STM32L431 keyboard controller. Their images and flash maps are not
interchangeable.

| Artifact / device | Deployment location | Contract |
|---|---|---|
| L496 EF800 Bootloader | Internal `0x08000000`, 64 KiB | Starts at hardware-reset handoff, supervises Main/Backup, update and boot-state policy. |
| L496 EF800 Backup | Internal `0x08010000`, 128 KiB | Recovery application; MTP access to Configuration and Shared Data. |
| L496 EF800 Application | Internal `0x08030000`, 832 KiB | Normal instrument; fixed address is a compatibility contract. |
| QSPI NVS | QSPI offset `0x000000`, 32 KiB | Boot selection/failure record only. |
| QSPI Configuration | QSPI offset `0x008000`, 992 KiB | Settings, composition, assignments, profiles, and workspace selection. |
| QSPI Shared Data | QSPI offset `0x100000`, 3072 KiB | Workspace/data, plugins, and Main firmware update staging. |
| Keyboard bootloader | L431 internal `0x08000000..0x08007FFF` | Resident boot/update and watchdog recovery. |
| Keyboard application | L431 from `0x08008000` | Matrix scan and keyboard UART protocol. |

Editable PlantUML source: [`physical_ef800.puml`](../diagram_as_code/07_deployment_view/physical_ef800.puml).

## Virtual EF800

The simulator and browser gateway are separate Compose runtime containers. The
gateway owns the private simulator TCP connection; browser clients use HTTP and
WebSocket routes only. Native-sim storage is disposable by default, and
persistent simulator flash must be selected explicitly. It must not be confused
with physical flash.

Editable PlantUML source: [`virtual_ef800.puml`](../diagram_as_code/07_deployment_view/virtual_ef800.puml).

## Build and test environment

The west workspace, Zephyr checkout, modules, Python environment, and compiler
are inside `EF800_Devcontainer`; the repository is mounted at
`/workspace/application`. Firmware builds, native Twister tests, image address/
size verification, and the Virtual EF800 Python suite are separate validation
gates. Hardware tests require explicitly selected ST-LINK probes and confirmed
target MCU identity. See [`../../AGENTS.md`](../../AGENTS.md) and
[`../../docs/testing.md`](../../docs/testing.md).
