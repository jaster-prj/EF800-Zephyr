# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-28
- Reviewer: OpenCode
- Change: add-composition-profile-mechanism

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - EF800Application retains hardware and persistence ownership.
- `adr/0002-isolate-external-plugin-execution.md` - Host-owned configuration and hardware boundaries remain outside plugins.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - Shared behavioral logic requires native-sim coverage.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - No browser or gateway provisioning surface is added.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - Production composition initialization must run with simulated storage as well as physical adapters.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - One EF800Application source composition serves both builds.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - Display-specific behavior remains outside this generic mechanism.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - Startup-only activation supports the established explicit restart boundary.
- `adr/0009-use-bounded-native-sim-log-stream.md` - Diagnostics remain bounded and do not become durable state.
- `adr/0010-plugin-owned-measurement-navigation.md` - Composition does not introduce plugin-specific host behavior.
- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - The private configuration profile is not exposed through current MTP roots.

## Repository-Level ADRs Created

- `adr/0012-use-file-backed-target-composition-profiles.md` - Use a recoverable file-backed COMP-Nr. profile, repository catalog, unknown-token preservation, and restart-scoped activation.

## Notes

No existing ADR is superseded. Concrete hardware definitions and consumers require later specifications and, where architecturally durable, later ADR review.
