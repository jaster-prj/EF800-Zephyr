# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-29
- Reviewer: OpenCode
- Change: edit-workspace-records

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - plugin-owned measurement UI boundary; workspace row editing remains host-owned.
- `adr/0002-isolate-external-plugin-execution.md` - plugin isolation; plugin records remain opaque workspace text.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - behavioral simulation context for regression coverage.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - front-panel test access context.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - production-path native-sim validation context.
- `adr/0006-use-single-ef800-main-target-for-physical-and-native-sim.md` - shared MainApplication implementation boundary.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - LCD renderer context for the editor display.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - native-sim lifecycle context.
- `adr/0009-use-bounded-native-sim-log-stream.md` - bounded simulator diagnostics context.
- `adr/0010-plugin-owned-measurement-navigation.md` - measurement navigation remains plugin-owned; workspace editing is generic and record-format agnostic.
- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - MTP shares filesystem-mediated access with MainApplication.
- `adr/0012-use-file-backed-target-composition-profiles.md` - recoverable file-update pattern for bounded persistent state.
- `adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md` - shared Data FAT storage ownership and MTP visibility.

## Repository-Level ADRs Created

- `adr/0014-journal-local-workspace-replacements.md` - use bounded, journaled same-volume replacement and optimistic source-version checks for local workspace mutations.

## Notes

All existing ADRs reviewed are accepted and have `Supersedes: None`; none is superseded. ADR-0014 records the new durable replacement decision without modifying prior ADRs.
