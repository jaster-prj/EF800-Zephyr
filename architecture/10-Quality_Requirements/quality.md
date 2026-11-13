# Quality Requirements

## Quality goals

| Priority | Quality | Scenario / architectural response |
|---|---|---|
| 1 | Data integrity | A failed update validation must not replace Main. A failed transfer must not publish a partial final file. A mount failure must not trigger arbitrary formatting. |
| 2 | Recoverability | Repeated Main watchdog failure selects a valid Backup image for one recovery boot; the next reset retries Main. Keyboard watchdog failures have an independent resident-loader recovery policy. |
| 3 | Compatibility | Fixed image addresses, keyboard frame contract, port assignment semantics, workspace data, and supported legacy settings are preserved or explicitly gated. |
| 4 | Availability | Optional peripheral and configuration failures should not block unrelated startup services; Backup remains available when Main cannot run. |
| 5 | Storage confidentiality / bounded exposure | MTP exposes only approved roots, not NVS/raw partitions. Browser gateway requests and transfers are bounded. |
| 6 | Testability | Shared domain code is tested with native Twister; a production-source EF800Application composition runs under `native_sim`. |
| 7 | Maintainability | Shared modules own domain logic; CMake source manifests and ADRs make platform seams and architectural changes visible. |

## Quality scenarios and verification

| Scenario | Verification | Limitation |
|---|---|---|
| Main image is invalid or repeatedly watchdog-resets. | Boot decision unit tests, image validation tests, controlled hardware boot/recovery test. | Native tests do not prove reset cause, watchdog, or vector handoff on silicon. |
| Update package is corrupt or truncated. | Package/parser and boot-update tests; image verification. | Physical QSPI write/erase and power-failure injection remain hardware gates. |
| Legacy FAT layout is present. | Signature detector and metadata tests; development-only reformat option check. | Real QSPI formatting has not been physically qualified; this operation destroys old data. |
| Browser requests a file operation. | Gateway tests and virtual USB behavioral tests. | Current virtual MTP enumeration timeout remains open; physical MTP/host matrix is incomplete. |
| Instrument or optional console is unavailable. | Native port/assignment tests and target diagnostics. | UART pin mux, signal integrity, and instrument electrical behavior require hardware. |
| External plugin fails or exceeds contract. | Host contract/unit and native behavioral tests. | MPU, ARM execution, fault containment, and cleanup require target validation. |

Test evidence boundaries are in [`../../docs/testing.md`](../../docs/testing.md);
QSPI and physical-storage gates are in
[`../../docs/qspi-migration.md`](../../docs/qspi-migration.md) and
[`../../docs/flash-layout.md`](../../docs/flash-layout.md).
