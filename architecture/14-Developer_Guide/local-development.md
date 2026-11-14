# Local Development

Firmware development uses the reusable `EF800_Devcontainer`; do not install or
run `west` directly on the Windows host. The detailed lifecycle, manifest checks,
build commands, image verification, flashing cautions, and test commands are
maintained in [`../../AGENTS.md`](../../AGENTS.md),
[`../../README.md`](../../README.md), and
[`../../docs/testing.md`](../../docs/testing.md).

## Repository layers

| Path | Role |
|---|---|
| `application/ef800-application/` | Main physical and native-sim application compositions. |
| `application/ef800-backup/` | Recovery MTP application. |
| `application/ef800-bootloader/` | L496 second-level bootloader. |
| `application/keyboard-application/` and `application/keyboard-bootloader/` | L431 keyboard application and resident bootloader. |
| `shared/` | Shared domain services, protocols, storage, update, and platform adapters. |
| `boards/` | EF800 board definitions and devicetree descriptions. |
| `tests/` | Native behavior and shared-component tests. |
| `GSE/Virtual-EF800/` | Browser gateway, simulator tools, and Python tests. |
| `adr/` | Accepted architecture decisions. |
| `architecture/` | Numbered arc42 sections and diagram sources. |

## Documentation change workflow

Update the relevant numbered arc42 section when a boundary or quality attribute
changes. Update the matching subsystem document and ADR as needed. Keep diagram
sources in `diagram_as_code/` aligned with section descriptions and refresh
rendered assets under `diagram_as_code/out/` when PlantUML is available.
