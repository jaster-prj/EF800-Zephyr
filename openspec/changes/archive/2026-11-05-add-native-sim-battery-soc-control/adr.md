# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-30
- Reviewer: OpenCode
- Change: add-native-sim-battery-soc-control

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - EF800 Application retains hardware and shared UI responsibilities.
- `adr/0002-isolate-external-plugin-execution.md` - Plugin-isolation behavior and hardware qualification remain out of scope.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - Native-sim is behavioral evidence, not hardware qualification.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - Browser control remains mediated by the gateway and bounded simulator connection.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - Native-sim exercises production device API paths through emulators.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - Physical and native-sim share the EF800 Application composition root.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - Display representation remains independent of battery state.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - Existing explicit simulator restart boundary remains intact.
- `adr/0009-use-bounded-native-sim-log-stream.md` - Browser diagnostics remain bounded and non-durable.
- `adr/0010-plugin-owned-measurement-navigation.md` - Plugin-owned navigation is unaffected.
- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - USB MTP ownership and restart boundaries are unaffected.
- `adr/0012-use-file-backed-target-composition-profiles.md` - BATSOC selections use the existing bounded, file-backed, restart-activated composition profile.
- `adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md` - Configuration storage remains in the private configuration FAT; the legacy-media handling portion is superseded by ADR-0017.
- `adr/0014-journal-local-workspace-replacements.md` - Workspace file transaction behavior is unaffected.
- `adr/0016-select-lcd-contrast-mode-through-composition.md` - Existing composition usage and startup activation pattern are retained; ADR-0016 supersedes ADR-0015.
- `adr/0017-development-auto-format-legacy-qspi.md` - Development-only legacy QSPI formatting policy is unaffected.

## Repository-Level ADRs Created

- `adr/0018-select-battery-soc-source-through-composition.md` - Define the BATSOC source group, runtime-unavailable behavior, separate ADC calibration, and shared native-sim source model.

## Notes

- ADR-0015 is historical and superseded by ADR-0016. ADR-0017 supersedes only the legacy-media handling decision in ADR-0013.
