# Risks and Technical Debt

| Risk / open issue | Impact | Mitigation / next evidence |
|---|---|---|
| QSPI part identity, JEDEC/SFDP behavior, capacity, erase geometry, frequency, signal integrity, and power-loss behavior are not fully production-qualified. | Corrupt data, failed boot metadata, or failed updates. | Keep release gates closed; validate fitted hardware, read/write/erase boundaries, partition formats, and interruption recovery before release. |
| Development Main image enables automatic formatting for a recognized legacy monolithic FAT layout. | All old volume contents are destroyed when the signature matches. | Never use it where data must be retained; disable `CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI` in production and verify effective `.config`. |
| Virtual USB host can time out during MTP interface enumeration despite device startup success. | Browser file operations may not complete in the simulator. | Investigate virtual USB enumeration independently; do not treat successful class startup as enumeration proof. |
| Physical MTP, MSC ownership switching, and host interoperability coverage is incomplete. | Service/host workflows can fail on target despite unit/native success. | Run controlled host matrix and transfer/disconnect/recovery tests with qualified board power and USB routing. |
| Main and Backup share QSPI media and cannot own it concurrently. | Incorrect application startup or uncoordinated USB ownership can cause filesystem corruption. | Retain boot handoff/restart boundary and enforce exclusive mount/MSC ownership. |
| Plugin MPU containment is a target-specific safety boundary. | A faulty package may impact Main if execution/cleanup is not contained. | Verify userspace domains, memory permissions, deadline behavior, fault classification, and safe thread/resource cleanup on L496. |
| EF800 Application has a large composition root. | Startup ordering and subsystem coupling are harder to reason about. | Keep domain services in shared modules, preserve source manifest, add focused tests, and incrementally extract cohesive orchestration services. |
| Zephyr and external module revisions move over time. | API or behavior drift can break build, MTP, or package integration. | Pin revisions in west manifest and update deliberately with complete builds/tests. |
| Simulator is behavioral, not a hardware emulator. | Passing tests can be mistaken for physical qualification. | Label evidence by environment and retain explicit hardware gates. |

Hardware evidence and release gates are maintained in
[`../../docs/qspi-migration.md`](../../docs/qspi-migration.md),
[`../../docs/flash-layout.md`](../../docs/flash-layout.md), and
[`../../docs/testing.md`](../../docs/testing.md).
