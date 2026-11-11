# MainApplication memory baseline

Captured 2026-11-08 from the EF800 MainApplication diagnostic build on the
STM32L496 (probe `066FFF504982494867074032`, device ID `0x461`). The image is
linked and programmed at `0x08030000`; image-layout verification passed with a
393,756-byte binary in the 832 KiB MainApplication slot. This diagnostic build
enables `CONFIG_INIT_STACKS` and adds the `memory stacks` shell command.

## Internal SRAM map

The linker reports a 256 KiB `RAM` region and a separate 64 KiB `SRAM1` region
(320 KiB total internal SRAM described for the MCU). MainApplication places
247,744 bytes in `RAM` (94.51%; end address `0x2003c7c0`) and no sections in
`SRAM1`. The remaining 14,400 bytes in the 256 KiB RAM region and the separate
64 KiB SRAM1 region are not represented as additional free heap; they remain
subject to the current linker/MPU layout. The fixed 32 KiB libc malloc arena
is already part of the RAM image footprint.

## Runtime stack watermark

The shell watermark uses Zephyr's stack sentinel scanning. Sizes below are
Zephyr-reported usable stack sizes; `used` is size minus reported unused space.
The first reading was taken after startup, followed by safe normal shell
operations (`keyboard version`, `rtc get`, `battery source get`) and another
reading. Values remained unchanged except shell use rose from 912 to 940 bytes.
This is a startup/normal-shell baseline, not a worst-case stress measurement;
plugin workloads, sustained USB transfers, and deep storage operations were
not exercised as a stress suite.

| Thread | Size (B) | Used high-water (B) | Unused (B) |
|---|---:|---:|---:|
| `main` | 8,128 | 5,300 | 2,828 |
| `ef800_shell` | 8,192 | 940 | 7,252 |
| `plugin_supervisor` | 4,032 | 800 | 3,232 |
| `usbd` | 4,096 | 2,304 | 1,792 |
| `sysworkq` | 1,024 | 536 | 488 |
| `mcumgr smp` | 1,984 | 208 | 1,776 |
| `usbd_msc` | 960 | 272 | 688 |
| `otgfs@50000000` | 448 | 256 | 192 |
| `idle` | 320 | 72 | 248 |
| unnamed | 960 | 616 | 344 |

## Measured buffer candidates and ownership/lifetime

Sizes are from linked ELF symbol sizes and the active target configuration.
These are candidates for a later pool migration, not decisions to move them.
Any object reachable by an ISR, DMA, USB controller, or MPU-domain thread must
retain suitable placement and lifetime guarantees.

| Candidate | Size | Owner and lifetime | Notes |
|---|---:|---|---|
| Plugin text arena | 32,768 B external; 16,384 B internal fallback | Plugin host; reused for one physical plugin image, from load until unload | MPU RX text; external placement must remain executable/aligned. Internal fallback is deliberately reduced. |
| Plugin rodata arena | 32,768 B external; 16,384 B internal fallback | Plugin host; reused with text for the active image | MPU RO; external placement retains read-only mapping/alignment. |
| Plugin mailbox region (includes plugin RW) | 16,384 B | Shared supervisor/worker protocol state, while the worker is active | Contains 4 KiB plugin RW plus mailbox/payload. Not a generic pool candidate; preserve its MPU partition and synchronization contract. |
| Input staging | 4,096 B | Privileged plugin syscall gateway; request/dispatch lifetime | Cleared and reused per gateway request. |
| Output staging | 4,096 B | Privileged plugin syscall gateway; response lifetime | Cleared and reused per gateway request; response must be copied before reuse. |
| Calculation snapshot | 4,096 B | Main UI/plugin host; one calculation request/result lifecycle | Copied workspace snapshot retained while asynchronous calculation completes. |
| Async calculation output | 4,096 B | Plugin supervisor/host; async request through UI consumption/cleanup | Must remain valid until result is consumed; do not alias request staging without changing ownership. |
| Plugin worker stack | 2,048 B | Unprivileged worker thread lifetime | Internal for now; stack placement in external SRAM has not been demonstrated. |
| Plugin supervisor stack | 4,096 B allocated | Supervisor thread lifetime | Runtime reports 4,032 usable bytes due to Zephyr stack metadata/guard. Keep internal pending explicit stack-region proof. |

The plugin text and rodata arenas have now been relocated when external SRAM is
validated. MainApplication obtains 32 KiB each from the external heap, sets
the worker MPU partitions to those exact aligned ranges, and retains 16 KiB
internal fallback arenas for a reduced profile. The ELF map drops from 247,744
to 214,976 bytes in the 256 KiB RAM region (32,768 bytes released).

The calculation snapshot and asynchronous calculation output are bounded 4 KiB
buffers with external-first allocation and statically allocated internal
fallbacks. Their request/result lifetimes are independent and do not alias
plugin syscall staging. The snapshot remains live through asynchronous worker
completion; output remains live until UI acceptance/discard releases the
result. Worker and supervisor stacks, mailbox, and syscall staging remain
internal because their thread/MPU/copy lifetimes have not been established as
safe for general-pool placement. The four external allocations request 72 KiB
total (32 KiB each for text/rodata and 4 KiB each for snapshot/output).
Accounting tracks requested bytes; the diagnostic probe is transient. Static
internal fallback storage remains reserved, so scratch relocation reduces
runtime pool pressure but not static internal RAM occupancy.

## Reproduction

Build with the EF800 `ef800-main` target and `CONFIG_INIT_STACKS=y`, verify
`scripts/verify-images.sh`, flash only at `0x08030000`, then run `memory stacks`
on the MainApplication console after startup and representative operations.
The diagnostic is temporary and should be removed or made explicitly
diagnostic-only after baseline collection is no longer needed.

## MainApplication pool bring-up

The physical MainApplication now enables the same FMC Bank 1 configuration as
the hardware-debug image, including its 80 MHz timing and FMC pinctrl. At
startup it checks FMC and SRAM device readiness, waits the IS61 power-up
interval, performs a 32-word write/read/restore check in the final 128 bytes of
SRAM before any pool use, and only then initializes a Zephyr heap over the
512 KiB external range. A failed check leaves that pool unavailable; current
static application and plugin storage remains internal, and core startup does
not depend on the external pool. The separate internal allocation wrapper uses
the configured 32 KiB libc arena.

On 2026-11-08 the pre-relocation pool image reported
`External SRAM validated: 524288 bytes` and `memory pool` successfully
allocated, wrote/read, and freed 32 bytes at `0x60000060`. After arena
relocation, the normal image reported `Plugin arenas profile=external` with
32 KiB RX and RO regions and continued to discover both packages. QSPI
configuration and shared-data volumes mounted, USB MTP and LCD initialized,
and the keyboard reported ready. Target evidence for the new calculation
scratch allocation and metadata-v4 admission checks remains pending. A separate test image with
`CONFIG_EF800_MAIN_SRAM_TEST_FAIL=y` exercised the failed-validation path: it
reported `internal-only profile active`, `memory pool` reported external
unavailable, and the same core storage, plugin discovery, USB, LCD, and
keyboard startup checkpoints completed. The test-only Kconfig option defaults
off; the normal, non-injected MainApplication was rebuilt and restored to
`0x08030000` after the fallback test. Current changes still require a fresh
target run before task 3.3 can be closed.
