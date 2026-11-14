# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-09
- Reviewer: OpenCode
- Change: plugin-owned-measurement-navigation

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - establishes package ownership of logical measurement UI/input while the host retains hardware and persistence boundaries.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - keeps this workflow behavioral native-sim evidence only.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - preserves gateway ownership of interactive simulator control.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - preserves production device/API boundaries.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - preserves shared EF800 Application composition.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - preserves the LCD transport contract.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - preserves startup-only package activation and restart boundaries.
- `adr/0009-use-bounded-native-sim-log-stream.md` - preserves bounded gateway diagnostics.

## Repository-Level ADRs Created

- `adr/0010-plugin-owned-measurement-navigation.md` - makes plugin metadata and plugin-owned UI the source of measurement assignment and navigation identity.

## Notes

The new ADR complements ADR-0001 by extending package-owned UI from submenu
behavior into global measurement assignment and navigation. No prior ADR was
modified or superseded.
