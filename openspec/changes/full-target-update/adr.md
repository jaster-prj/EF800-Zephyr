# ADR Review Manifest

## ADR Review Completed

- Date: 2026-11-13
- Reviewer: OpenCode
- Change: full-target-update

## In-Force ADR Context Reviewed

- adr/0001-package-owned-measurement-ui.md - unrelated application UI boundary
- adr/0002-isolate-external-plugin-execution.md - plugin isolation remains unchanged
- adr/0003-use-native-sim-for-behavioral-ef800.md - native test evidence boundary
- adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md - simulator gateway unrelated
- adr/0005-use-zephyr-device-emulation-for-production-native-sim.md - native test evidence boundary
- adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md - shared EF800Application composition
- adr/0007-use-configurable-packed-t6963-renderer.md - unrelated display contract
- adr/0008-use-native-sim-package-control-and-restart-boundary.md - simulator package boundary unrelated
- adr/0009-use-bounded-native-sim-log-stream.md - unrelated diagnostics
- adr/0010-plugin-owned-measurement-navigation-and-assignment.md - unrelated navigation ownership
- adr/0011-use-mtp-for-concurrent-usb-file-access.md - MTP filesystem-only staging and MSC boundary
- adr/0012-use-file-backed-target-composition-profiles.md - unrelated composition persistence
- adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md - existing image map, update scope, and storage ownership; superseded by this change's explicit full-update decision
- adr/0014-journal-local-workspace-replacements-on-shared-data.md - filesystem journal precedent, not directly reused
- adr/0015-separate-runtime-lcd-contrast-control.md - superseded historical display decision
- adr/0016-select-lcd-contrast-mode-through-composition.md - unrelated current display composition decision
- adr/0017-development-auto-format-legacy-qspi.md - QSPI migration exception remains unchanged
- adr/0018-select-battery-soc-source-through-composition.md - unrelated battery source decision
- adr/0019-establish-ef800-c-quality-profile.md - C quality scope applies
- adr/0020-organize-ef800-firmware-by-domain-ownership.md - update coordinator should follow domain ownership

## Repository-Level ADRs Created

- adr/0021-coordinate-complete-firmware-updates-from-staged-packages.md - establish staged-package coordination and supersede ADR-0013's restrictive update scope while retaining layout and storage boundaries

## Notes

The supersession is intentional: the fixed flash map, QSPI partitioning, MTP boundaries, and sole-owner filesystem rules remain; only the prior restriction on bootloader and BackupApplication updates changes. Bootloader self-update remains gated on a separately proven safe programming mechanism.
