# Architectural Decisions

The accepted decisions below shape the current architecture. Full rationale and
consequences remain in the linked ADR files.

| Decision | Architectural effect |
|---|---|
| [ADR-0001 — package-owned measurement UI](../../adr/0001-package-owned-measurement-ui.md) | Measurement UI behavior belongs to packages within the host-owned framework. |
| [ADR-0002 — isolate external plugin execution](../../adr/0002-isolate-external-plugin-execution.md) | Defines the intended unprivileged worker/MPU containment policy and rejects unsafe compatibility fallback; target proof remains required. |
| [ADR-0003 — native-sim behavioral EF800](../../adr/0003-use-native-sim-for-behavioral-ef800.md) | Provides a behavioral platform for production application orchestration. |
| [ADR-0004 — web gateway front panel](../../adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md) | Browser access is mediated through the gateway rather than direct simulator control. |
| [ADR-0005 — Zephyr device emulation](../../adr/0005-use-zephyr-device-emulation-for-production-native-sim.md) | Native-sim peripheral behavior follows Zephyr device abstractions and emulation seams. |
| [ADR-0006 — one Main target](../../adr/0006-use-single-ef800-application-target-for-physical-and-native-sim.md) | Physical and native-sim Main share a production source set. |
| [ADR-0008 — native-sim package control/restart](../../adr/0008-use-native-sim-package-control-and-restart-boundary.md) | Package discovery is startup-scoped; simulator restart is explicit. |
| [ADR-0011 — MTP for concurrent USB file access](../../adr/0011-use-mtp-for-concurrent-usb-file-access.md) | File access is filesystem-mediated rather than exposing the disk as raw MSC. |
| [ADR-0012 — file-backed target composition](../../adr/0012-use-file-backed-target-composition-profiles.md) | Hardware composition is bounded, validated, persistent, and restart-scoped. |
| [ADR-0013 — partition QSPI for recovery and shared MTP](../../adr/0013-partition-qspi-for-recovery-and-shared-mtp-access.md) | Establishes QSPI ownership, partition map, and Main/Backup MTP roots. |
| [ADR-0014 — journal local workspace replacement](../../adr/0014-journal-local-workspace-replacements.md) | Defines transactional workspace replacement semantics. |
| [ADR-0017 — development auto-format legacy QSPI](../../adr/0017-development-auto-format-legacy-qspi.md) | Adds destructive development-only formatting for a validated legacy layout; production must disable it. |

ADR-0015/0016 define LCD contrast control and composition details. The full
decision log is in [`../../adr/`](../../adr/).
