# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-07
- Reviewer: OpenCode
- Change: native-sim-peripheral-emulation

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves package-owned logical measurement UI while hardware, raw keyboard, LCD, and global navigation remain host responsibilities
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - establishes native_sim as the behavioral simulator and preserves the hardware qualification boundary
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - keeps browser presentation in the gateway and the simulator protocol bounded and headless-compatible

## Repository-Level ADRs Created

- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - commits the Virtual EF800 to production EF800 Application execution through Zephyr-compatible emulated and simulated peripherals

## Notes

ADR 0005 complements rather than supersedes ADR 0003: it makes the virtual adapter boundary concrete at the Zephyr device/API level without introducing STM32 register emulation or changing the qualification boundary.
