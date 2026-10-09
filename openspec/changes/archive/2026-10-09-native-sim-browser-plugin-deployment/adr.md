# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-08
- Reviewer: OpenSpec planning workflow
- Change: native-sim-browser-plugin-deployment

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves host ownership of LCD, keyboard, navigation, and resources.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - limits native-sim results to behavioral evidence.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - keeps browser access behind the web gateway and simulator connection boundary.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - requires production API paths and explicit virtual devices.
- `adr/0006-use-single-ef800-main-target-for-physical-and-native-sim.md` - preserves one production composition root and target-specific adapters.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - preserves the packed framebuffer and virtual display architecture.

## Repository-Level ADRs Created

- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - establishes native-sim-specific PMP packages, private bounded browser-to-simulator package control, atomic virtual-FAT installation, and explicit restart activation.

## Notes

The new decision extends the existing startup-only plugin lifecycle and does not supersede any prior ADR.
