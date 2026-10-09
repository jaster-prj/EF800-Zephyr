# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-08
- Reviewer: OpenCode
- Change: expand-virtual-ef800-browser-keyboard

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves host ownership of raw keyboard framing and application input boundaries.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - keeps browser input as behavioral native-sim evidence only.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - retains the gateway-owned simulator connection, controller lease, and held-input cleanup boundary.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - preserves production MainApplication and virtual device boundaries.
- `adr/0006-use-single-ef800-main-target-for-physical-and-native-sim.md` - keeps this change limited to virtual front-panel infrastructure and browser behavior.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - preserves the existing complete LCD frame contract.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - leaves package and simulator restart controls separate from keyboard input.
- `adr/0009-use-bounded-native-sim-log-stream.md` - preserves the single gateway connection and bounded browser transport.

## Repository-Level ADRs Created

- None: no major durable architectural decisions were introduced by this change.

## Notes

The change extends the existing browser keypad contract and canonical key
validation without introducing a new protocol, service boundary, or durable
system pattern.
