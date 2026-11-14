# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-22
- Reviewer: OpenCode
- Change: `add-hot-switchable-usb-mtp`

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves host ownership of hardware and persistence boundaries.
- `adr/0002-isolate-external-plugin-execution.md` - keeps storage qualification and package activation separate from execution isolation.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - retains physical USB as a hardware qualification gate.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - leaves the virtual gateway boundary unchanged.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - keeps native storage and USB evidence behavioral only.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - requires target-specific USB adapters within the shared EF800 Application target.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - no direct USB-storage constraint.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - preserves startup-only plugin activation and atomic package installation principles.
- `adr/0009-use-bounded-native-sim-log-stream.md` - leaves physical console routing distinct from native-sim diagnostics.
- `adr/0010-plugin-owned-measurement-navigation.md` - preserves host-owned storage and resource assignment.

## Repository-Level ADRs Created

- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - selects filesystem-mediated MTP, serialized hot-switching, atomic uploads, and a restart boundary for MSC ownership changes.

## Notes

No existing ADR is superseded. The new decision complements the established physical-hardware qualification and startup-only plugin activation boundaries.
