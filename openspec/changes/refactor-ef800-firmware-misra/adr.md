# ADR Review Manifest

## ADR Review Completed

- Date: 2026-11-11
- Reviewer: OpenSpec planning
- Change: `refactor-ef800-firmware-misra`

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - plugins own measurement presentation/input; host retains hardware and resources.
- `adr/0002-isolate-external-plugin-execution.md` - preserve plugin isolation, supervision, and host-owned recovery.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - native-sim is behavioral evidence, not hardware qualification.
- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - preserve virtual front-panel gateway boundaries.
- `adr/0005-use-zephyr-device-emulation-for-production-native-sim.md` - preserve production composition with explicit emulated devices.
- `adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md` - retain one EF800Application target and shared source manifest.
- `adr/0007-use-configurable-packed-t6963-renderer.md` - preserve packed, configurable T6963 rendering behavior.
- `adr/0008-use-native-sim-package-control-and-restart-boundary.md` - preserve virtual plugin deployment/restart boundary.
- `adr/0009-use-bounded-native-sim-log-stream.md` - preserve bounded native-sim diagnostics and physical-console separation.
- `adr/0010-plugin-owned-measurement-navigation.md` - preserve plugin identity, assignment, and host navigation/resource boundary.
- `adr/0011-use-mtp-for-concurrent-usb-file-access.md` - preserve MTP filesystem access and MSC restart ownership boundary.
- `adr/0012-use-file-backed-target-composition-profiles.md` - preserve versioned, recoverable composition profile behavior.
- `adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md` - preserve QSPI/internal-flash partitioning and storage ownership.
- `adr/0014-journal-local-workspace-replacements.md` - preserve journaled workspace replacement and conflict semantics.
- `adr/0015-separate-runtime-lcd-contrast-control.md` - preserve separation of contrast state from display content.
- `adr/0016-select-lcd-contrast-mode-through-composition.md` - preserve composition-controlled contrast mode.
- `adr/0017-development-auto-format-legacy-qspi.md` - preserve development-only formatting safeguards and production restrictions.
- `adr/0018-select-battery-soc-source-through-composition.md` - preserve composition-selected battery source behavior.

No `Supersedes` links were present in the existing ADR set; all listed ADRs were treated as in force.

## Repository-Level ADRs Created

- `adr/0019-establish-ef800-c-quality-profile.md` - establish practical style/quality checks and finding dispositions for EF800-owned C; no formal MISRA edition is required.
- `adr/0020-organize-ef800-firmware-by-domain-ownership.md` - establish domain-owned modules and behavior-preserving refactoring.

## Notes

No single analyzer is selected. Cppcheck is one candidate for useful style/quality checks, but its first Zephyr compile-database parsing trial failed on compiler/configuration macros. Tool selection remains open pending the practical pilot. This change makes no formal MISRA compliance claim.
