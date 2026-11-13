# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-29
- Reviewer: OpenCode
- Change: add-runtime-lcd-contrast-control

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - EF800Application retains LCD and hardware ownership.
- `adr/0002-isolate-external-plugin-execution.md` - No impact to plugin isolation boundaries.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - Native-sim remains behavioral, not hardware qualification.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - Browser presentation belongs to the gateway.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - Native-sim exercises production EF800Application through emulated devices.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - EF800Application source composition is shared across targets.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - Preserve the packed one-bit framebuffer and existing frame format.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - Contrast does not alter plugin restart lifecycle.
- `adr/0009-use-bounded-native-sim-log-stream.md` - Reuse the bounded front-panel connection; keep diagnostics independent.
- `adr/0010-plugin-owned-measurement-navigation.md` - No impact to plugin-owned measurement navigation.
- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - Configuration remains filesystem-mediated and private to Main MTP.
- `adr/0012-use-file-backed-target-composition-profiles.md` - Use the staged, verified profile and restart activation boundary for COMPOSITION changes.
- `adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md` - Use the existing Configuration FAT; do not repartition or format QSPI.
- `adr/0014-journal-local-workspace-replacements.md` - No impact to workspace transaction behavior.

## Repository-Level ADRs Created

- `adr/0016-select-lcd-contrast-mode-through-composition.md` - Select LCDCONTR mode from the active composition profile and keep duty independently persisted.

## Notes

- ADR-0015 described live LCDCONTR changes outside COMPOSITION. It is superseded by ADR-0016; its file remains unchanged as decision history.
