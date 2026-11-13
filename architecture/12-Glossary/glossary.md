# Glossary

| Term | Meaning |
|---|---|
| EF800 | The measurement instrument and this firmware migration project. |
| EF800Application / Main | Normal L496 instrument runtime, linked at `0x08030000`. |
| BackupApplication / Backup | One-boot L496 recovery runtime at `0x08010000`. |
| Second-level Bootloader | Zephyr boot manager at `0x08000000`; runs after STM32 hardware boot. |
| Keyboard controller | Separate STM32L431 scanning keypad and speaking the EF800 UART protocol. |
| QSPI | External 4 MiB NOR represented by three software partitions. |
| Boot-state NVS | Dedicated 32 KiB nonvolatile record store for boot target/failure metadata. |
| Configuration FAT | 992 KiB FAT region for private settings, assignments, profiles, and configuration. |
| Shared Data FAT | 3072 KiB FAT region for workspace/data, plugins, and firmware update staging. |
| MTP | Media Transfer Protocol; EF800 uses named filesystem roots, not raw block access. |
| MSC | USB Mass Storage Class; exposes raw storage and therefore requires exclusive ownership. |
| PluginManager | Pinned west module that discovers and activates EF800-compatible `.PMP` packages. |
| PMP | EF800 plugin package file, with target-specific ARM or native-sim image content. |
| Native-sim | Zephyr host target used for behavioral testing with emulated devices and peers. |
| Virtual EF800 | EF800Application native-sim plus private simulator protocol, gateway, and browser UI. |
| Storage owner | Runtime state indicating whether Shared Data is owned by FAT or USB MSC. |
| Composition profile | Bounded configuration of supported hardware/behavior groups activated on restart. |
| Release gate | Required hardware evidence or configuration condition before production use. |
