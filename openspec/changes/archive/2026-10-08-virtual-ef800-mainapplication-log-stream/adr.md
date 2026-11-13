# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-08
- Reviewer: OpenCode
- Change: virtual-ef800-applicationapplication-log-stream

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves host ownership of hardware and UI boundaries
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - keeps this feature native-sim behavioral evidence only
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - preserves the gateway-owned single simulator connection
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - remains within the production native-sim emulation boundary
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - keeps shared EF800Application behavior and target-specific adapters
- `adr/0007-use-configurable-packed-t6963-renderer.md` - unchanged display transport and renderer boundary
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - preserves bounded private simulator control and restart behavior

## Repository-Level ADRs Created

- `adr/0009-use-bounded-native-sim-log-stream.md` - routes bounded native-sim diagnostics through the existing front-panel gateway connection

## Notes

The new decision is additive and does not supersede any existing ADR.
