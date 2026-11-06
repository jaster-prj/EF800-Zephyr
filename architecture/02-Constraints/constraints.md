# Architecture Constraints

| Constraint | Consequence |
|---|---|
| Main/debug controller is STM32L496ZGT6 with 1 MiB internal flash. | Internal image addresses and sizes are fixed: Bootloader 64 KiB, Backup 128 KiB, Main 832 KiB. Main is linked at `0x08030000`. |
| Keyboard controller is STM32L431. | It has its own resident bootloader and relocated application; keyboard packages cannot be substituted for L496 images. |
| External QSPI is represented as a 4 MiB device. | Software reserves NVS `0x000000..0x007FFF`, Configuration FAT `0x008000..0x0FFFFF`, and Shared Data FAT `0x100000..0x3FFFFF`. Fitted-part geometry and power-loss behavior still require qualification. |
| QSPI is shared between boot, main, recovery, and USB workflows. | Bootloader and Backup check for legacy media; Main and Backup mount FAT without mount-failure formatting. Main development reformat requires a recognized legacy signature and an explicit Kconfig option. |
| FAT and USB MSC cannot safely own the shared disk concurrently. | Physical FAT-to-MSC transfer requires unmount-before-MSC. MSC-to-FAT is restart-gated where the USB worker may still be active. MTP is filesystem-mediated. |
| Device-next MTP is supplied by the pinned `zephyr-mtp` west module. | Integration follows that module's API and must be revisited as Zephyr support evolves. |
| External packages use the pinned PluginManager module and an EF800 contract. | Package format, target ABI, allowed services, image bounds, and discovery path are integration constraints. Native `.PMP` packages are not physical ARM artifacts. |
| Physical and simulated builds share MainApplication production sources. | Platform-specific adapters and devicetree differ; behavioral similarity is intentional, hardware equivalence is not implied. |
| Firmware builds are performed in the reusable Zephyr devcontainer. | Do not install/run west on the Windows host. Manifest revisions and container state are part of reproducible build setup. |
| Engineering firmware can contain destructive QSPI behavior. | `CONFIG_EF800_MAIN_REFORMAT_LEGACY_QSPI` is development-only and must be disabled for production/data-preserving use. |

Operational details are in [`../../AGENTS.md`](../../AGENTS.md),
[`../../docs/flash-layout.md`](../../docs/flash-layout.md),
[`../../docs/qspi-migration.md`](../../docs/qspi-migration.md), and
[`../../docs/usb-mtp.md`](../../docs/usb-mtp.md).
