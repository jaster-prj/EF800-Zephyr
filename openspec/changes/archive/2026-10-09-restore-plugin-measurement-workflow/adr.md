# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-09
- Reviewer: OpenSpec planning workflow
- Change: restore-plugin-measurement-workflow

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - keeps instrument UI and logical input package-owned while the host owns hardware and global workflow boundaries.
- `adr/0002-isolate-external-plugin-execution.md` - preserves the external plugin execution and host-service isolation boundary.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - governs behavioral native-sim coverage.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - governs virtual front-panel integration tests.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - governs production native-sim peripheral emulation.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - keeps physical and native-sim EF800Application behavior aligned.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - governs display rendering behavior used by workflow tests.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - governs native-sim package lifecycle control.
- `adr/0009-use-bounded-native-sim-log-stream.md` - governs bounded native-sim diagnostics.
- `adr/0010-plugin-owned-measurement-navigation.md` - keeps plugin identity and instrument action navigation generic and host resource ownership explicit.

## Repository-Level ADRs Created

- None: no major durable architectural decisions were introduced by this change.

## Notes

The explicit separation between plugin instrument actions and host-owned pending-measurement actions is a workflow correction within the existing package-owned UI and host-resource boundaries, not a replacement for those decisions.
