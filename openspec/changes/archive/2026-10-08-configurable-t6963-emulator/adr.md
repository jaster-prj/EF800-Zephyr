# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-08
- Reviewer: OpenSpec planning workflow
- Change: configurable-t6963-emulator

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - confirms raw LCD hardware remains host-owned.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - limits this work to behavioral simulation evidence.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - preserves the gateway and browser boundary.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - requires production driver/API paths and explicit virtual devices.
- `adr/0006-use-single-ef800-main-target-for-physical-and-native-sim.md` - keeps shared production composition and virtual infrastructure.

## Repository-Level ADRs Created

- `adr/0007-use-configurable-packed-t6963-renderer.md` - establishes configurable caller-owned T6963 RAM, a packed one-bit canonical framebuffer, dirty-region rendering, and complete-frame publication on actual pixel changes.

## Notes

The new decision is compatible with the reviewed in-force ADRs and does not supersede any prior decision.
