# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-09
- Reviewer: OpenCode
- Change: workspace-file-browser

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - preserves host ownership of display hardware and global navigation.
- `adr/0002-isolate-external-plugin-execution.md` - preserves plugin isolation and host-resource boundaries.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - requires behavioral coverage through native simulation.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - defines the virtual front-panel boundary.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - keeps native-sim on production application paths.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - keeps shared EF800 Application composition.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - preserves production display rendering in simulation.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - preserves startup/package lifecycle boundaries.
- `adr/0009-use-bounded-native-sim-log-stream.md` - supports bounded virtual-sim observability.
- `adr/0010-plugin-owned-measurement-navigation.md` - preserves plugin-owned measurement behavior and host-owned global navigation.

## Repository-Level ADRs Created

- None: no major durable architectural decisions were introduced by this change.

## Notes

The design now selects a fixed maximum 4096-byte working buffer with `(start, end)` file-position ranges for adjacent-line navigation. This is scoped to the workspace viewer and persistence integration in this change and does not introduce a repository-wide durable architectural decision.
