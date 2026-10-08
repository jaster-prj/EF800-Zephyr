# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-07
- Reviewer: OpenCode
- Change: unify-ef800-main-native-sim

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves package-owned logical measurement UI and host ownership of hardware/global navigation
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - establishes native_sim behavioral simulation and its hardware qualification boundary
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - keeps browser presentation in the gateway
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - establishes Zephyr-compatible device emulation for production MainApplication execution

## Repository-Level ADRs Created

- `adr/0006-use-single-ef800-main-target-for-physical-and-native-sim.md` - makes `application/ef800-main` the shared physical/native-sim MainApplication target

## Notes

ADR 0006 complements ADR 0005 by consolidating the application composition while retaining the device-emulation boundary.
