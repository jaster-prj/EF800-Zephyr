# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-28
- Reviewer: OpenCode
- Change: `partition-qspi-and-add-mtp-recovery`

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - retains host ownership of persistence and hardware boundaries.
- `adr/0002-isolate-external-plugin-execution.md` - leaves plugin isolation and physical qualification gates unchanged.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - defines native-sim as behavioral evidence, not hardware qualification.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - keeps the browser behind the gateway and the simulator endpoint private.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - requires native-sim to execute production EF800 Application through Zephyr device APIs.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - keeps physical and native-sim EF800 Application source composition aligned.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - no storage or USB conflict.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - preserves startup-only plugin discovery and bounded gateway control.
- `adr/0009-use-bounded-native-sim-log-stream.md` - retains the single gateway-owned simulator connection for diagnostics.
- `adr/0010-plugin-owned-measurement-navigation.md` - retains host-owned assignment and resource boundaries.
- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - preserves EF800 Application's approved MTP roots and MSC restart ownership boundary.
- `adr/0012-use-file-backed-target-composition-profiles.md` - keeps composition profiles file-backed and restart-scoped in private configuration.

## Repository-Level ADRs Created

- `adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md` - partitions QSPI and defines recovery MTP, boot-state, and shared-data ownership.

## Notes

ADR-0013 complements ADR-0011: Configuration is available through the separate EF800 Backup recovery MTP context, while EF800 Application MTP remains limited to Data, Plugins, and FWU.
