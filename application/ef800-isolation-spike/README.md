# EF800 isolation spike

The spike has a NUCLEO-F767ZI configuration for early MPU development and an
EF800 STM32L496 configuration for target-specific validation. The EF800 test
image is linked and programmed at `0x08000000` for direct bring-up. This
replaces the bootloader image during testing; the bootloader, backup, and main
images must all be restored after the external-SRAM change's target testing is
finished. Do not leave this temporary test image installed for normal use.

It exercises:

- ARMv7-M MPU and Zephyr userspace setup;
- execute-XOR-write configuration;
- an unprivileged worker thread;
- RX/RW memory-domain partitions;
- the NUCLEO board's auxiliary UART (`USART6`). The EF800 L496 spike keeps
  `USART1` silent because those pins are an assignable measurement-port route;
  its diagnostics use the selected console instead.

On the Nucleo F767ZI, the board's ST-LINK virtual COM port is `USART3`. The auxiliary UART is
`USART6`, using the board DTS pins `PG14=TX` and `PG9=RX`, at 115200 8N1.
Connect a 3.3 V USB-UART adapter to those pins and keep its ground connected
to the Nucleo ground.

## Build

Run inside the reusable development container. For the EF800 L496 target:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-isolation-spike -d application/build/ef800-isolation-spike-l496
```

The EF800 board config links the temporary test image at `0x08000000` and
enables the external-SRAM configuration. The NUCLEO-F767ZI build remains
available:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b nucleo_f767zi application/application/ef800-isolation-spike -d application/build/ef800-isolation-spike-f767
```

The NUCLEO-F767ZI image is linked for its internal flash at `0x08000000`.

For the L496 baseline fault-mode-0 build, Zephyr reports 51,908 bytes of Flash
and 20,608 bytes of RAM used. The worker stack is 2,048 bytes. Cortex-M4
provides eight MPU regions; the plugin domain uses four explicit partitions
(RX text, RO/XN constants, RW/XN state, RW/XN mailbox), plus the user stack and
its MPU guard. Together with the two fixed Flash/SRAM regions, this fills the
eight-region budget. The image prints the actual region count and stack size
on the board; this build/map budget is not a substitute for confirming the
L496 runtime configuration on hardware.

This minimal spike disables Zephyr thread-local storage because its worker
does not call libc. Production plugin TLS/libc requirements must be budgeted
as a specific bounded partition; the spike must not grant an oversized libc
range that overlaps MainApplication data.

For the P-NUCLEO-WB55 board, the generic worker/fault path can also be built
with the Zephyr `nucleo_wb55rg` target:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b nucleo_wb55rg application/application/ef800-isolation-spike -d application/build/ef800-isolation-spike-wb55-read -- -DCONFIG_EF800_SPIKE_FAULT_MODE=1
```

The current WB55 image is linked at `0x0800c000` (the board's selected slot0),
and the board console is exposed by its ST-LINK VCP. This can exercise the
Zephyr fatal-hook/worker-abort mechanism on a second Cortex-M4 target, but does
not substitute for the STM32L496 MPU-region and fault validation.

WB55 test images were programmed only at `0x0800c000` and launched directly
through SWD by setting VTOR/MSP/PC; the bootloader was not executed or
programmed. The probe identified `Device ID 0x495`, `STM32WB5x/35xx` at 3.26 V.
These observations are cross-target evidence only and do not complete any
STM32L496 validation task:

| WB55 mode | Observation |
|---:|---|
| 0 | Unprivileged worker baseline passed (`worker user=1 result=0x5a PASS`). |
| 1 | Protected host read fault: reason 19 (`K_ERR_ARM_MEM_DATA_ACCESS`) at `0x20000430`; Zephyr aborted the worker and notified the supervisor. |
| 2 | Protected host write fault: reason 19 (`K_ERR_ARM_MEM_DATA_ACCESS`) at `0x20000430`; Zephyr aborted the worker and notified the supervisor. |
| 3 | Execute-never fault: reason 20 (`K_ERR_ARM_MEM_INSTRUCTION_ACCESS`) at `0x20000c00`; Zephyr aborted the worker and notified the supervisor. |
| 4 | UDF fault: reason 36 (`K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION`) at `0x20000000`; Zephyr aborted the worker and notified the supervisor. |
| 5 | Direct write to the 64-byte privileged-stack guard at `0x20004000`: reason 19, valid stacked PC `0x20000006`, `worker was unprivileged=1`; Zephyr aborted the worker and the supervisor reported the fault. |
| 6 | Deep overflow reported a stacking error and data access violation at `0x20002f74`, with stacked PC `0x00000000`; the narrow hook did not recover or notify the supervisor. |

The mode-5 guard write confirms recoverable direct MPU-guard access behavior on
WB55. Mode 6 is intentionally separate: its invalid exception frame is not
treated as a recoverable worker fault. These WB55 results do not replace the
L496-specific MPU-permission evidence in task 1.2 or the fault-boundary results
recorded below.

### EF800 STM32L496 bench results

The L496 baseline ran with `userspace=1`, `arm_mpu=1`, `xor_write=1`, eight
hardware MPU regions, 32-byte minimum MPU alignment, and stack guards enabled.
The printed partitions were RX text `0x20000000/1024`, RO/XN constants
`0x20001000/1024`, RW/XN state `0x20000c00/1024`, RW/XN mailbox
`0x20000800/1024`, and worker stack `0x20003000/2048`; the worker completed as
`user=1 result=0x5a PASS`.

Each fault mode below was built separately, programmed only to the L496
MainApplication slot at `0x08030000`, and observed on UART4/COM7 at 115200 8N1.
Modes 1–5 returned to the privileged spike supervisor after Zephyr aborted the
faulting worker. The fatal hook recorded only the reason and stacked PC, then
returned to Zephyr's thread-abort path; the supervisor received its semaphore
notification and reported the worker as unprivileged.

| Mode | L496 observation |
|---:|---|
| 1 protected host read | MPU Data Access Violation, reason 19 (`K_ERR_ARM_MEM_DATA_ACCESS`), MMFAR `0x20000430`, worker PC `0x20000004`; supervisor notified. |
| 2 protected host write | MPU Data Access Violation, reason 19, MMFAR `0x20000430`, worker PC `0x20000004`; supervisor notified. |
| 3 execute from RW/XN | MPU Instruction Access Violation, reason 20 (`K_ERR_ARM_MEM_INSTRUCTION_ACCESS`), PC `0x20000c00`; supervisor notified. |
| 4 undefined Thumb instruction | Usage Fault, reason 36 (`K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION`), PC `0x20000000`; supervisor notified. |
| 5 write privileged-stack guard | MPU Data Access Violation, reason 19, MMFAR `0x20004000`, worker PC `0x20000006`; the reported guard was 64 bytes and the supervisor was notified. |
| 6 deep stack overflow (initial run before reboot fallback) | MPU stacking error plus Data Access Violation, MMFAR `0x20002f74`, reported PC `0x1c4bb538`; no `worker fault reported` line or supervisor notification followed. The target remained in the fatal path and required ST-LINK reprogramming. This is intentionally not classified as a recoverable plugin fault. |

After mode 6, the normal worker-disabled MainApplication image was rebuilt,
verified linked at `0x08030000`, programmed with the L496 probe, and observed
booting normally on COM7. Product userspace execution remains disabled. These
results establish the L496 behavior of the recoverable and stack-fault spike
cases; the additional MPU permission probes are recorded next. Production
recovery integration remains open.

### Additional MPU boundary probes

Modes 12–16 were built separately and run on the L496. All five faults were
attributed to the unprivileged worker, reported to the spike supervisor, and
terminated through Zephyr's thread-abort path. None accessed its target or
caused a reboot.

| Mode | L496 observation |
|---:|---|
| 12 write RO/XN constants | Data Access Violation, reason 19, MMFAR `0x20001000`, worker PC `0x20000004`; supervisor notified. |
| 13 execute RW/XN mailbox | Instruction Access Violation, reason 20, worker PC `0x2000081c`; supervisor notified. The mailbox remained readable/writable in normal worker operation. |
| 14 read kernel thread object | Data Access Violation, reason 19, MMFAR `0x20001400` (`plugin_thread`), worker PC `0x20000004`; supervisor notified. |
| 15 read MMIO | Data Access Violation, reason 19, MMFAR `0x40000000`, worker PC `0x20000006`; supervisor notified before any peripheral read completed. |
| 16 read unmapped plugin-sized buffer | Data Access Violation, reason 19, MMFAR `0x20000c00`, worker PC `0x20000004`; supervisor notified. The 1 KiB buffer was outside the worker's memory domain. |

Together with baseline worker execution from RX text, normal RW state/mailbox
access, the mode-3 state execute-never fault, mode-5 stack-guard fault, and
modes 1–2 protected host-RAM faults, these observations demonstrate the
configured L496 user partitions and deny access to the tested host, kernel,
MMIO, and unmapped-plugin addresses. This completes the spike evidence for
OpenSpec task 1.2; it does not qualify arbitrary package loading or production
recovery.

The follow-up fatal-boundary probes were then built and run on the same L496
probe, also at `0x08030000`:

| Mode | L496 observation |
|---:|---|
| 8 privileged thread | Usage Fault, reason 36, PC `0x0803099a`; no worker notification, followed by `CONTROLLED_FATAL_REBOOT_CONFIRMED`. |
| 9 ISR | Usage Fault, reason 36, PC `0x08038762`, stacked xPSR `0x2100000f` (IPSR 15); no worker notification, followed by controlled reboot confirmation. |
| 10 nested | Initial worker Usage Fault at `0x20000000`, followed by HardFault escalation and another Usage Fault at `0x0803086e`; no worker notification, followed by controlled reboot confirmation. |
| 11 unattributable worker | Usage Fault, reason 36, PC `0x080307bc` in the worker trampoline, outside plugin text; no worker notification, followed by controlled reboot confirmation. |

These probes confirm the spike's narrow attribution checks reject privileged,
ISR, nested, and out-of-image faults. The unclassified-fault path now keeps the
architecture's fatal log, stores a mode-tagged one-shot marker in RTC backup
registers, and calls `sys_reboot(SYS_REBOOT_COLD)`. The bootloader hands control
back to the same test image; it recognizes the marker, reports
`CONTROLLED_FATAL_REBOOT_CONFIRMED`, stays disarmed, and feeds the enabled IWDG
until the test image is replaced. The marker is spike-only and is not a
production crash/quarantine policy.

Mode 6 was also repeated with this fatal policy: the MPU stacking error/data
access violation at `0x20002f74` (reported PC `0x43082102`, invalid exception
frame) still produced no worker notification, then the target rebooted and
confirmed the marker. After the probes, the normal
worker-disabled MainApplication was restored at `0x08030000` and observed
booting normally. Before restoration, a current mode-0 baseline image cleared
the spike-only backup-register marker and passed `worker user=1 result=0x5a`.
Rebuild/run mode 0 before repeating the same fault mode if its marker is still
present.

### EF800 external-SRAM MPU validation

The EF800-specific build enables FMC before accessing the external region and
places the spike's 1 KiB RX plugin-text section at `0x60000000`. The RO/XN,
RW/XN, and mailbox partitions are at `0x60001000`, `0x60002000`, and
`0x60004000`; `0x60003000` is deliberately mapped in physical SRAM but omitted
from the worker domain. The user stack remains internal. The image checks FMC
and SRAM device readiness, waits the required power-up interval, and clears
only its assigned external test partitions before creating the worker domain.

On 2026-11-08, the image was built in the devcontainer and run on the confirmed
L496 target (probe `066FFF504982494867074032`, device ID `0x461`) at
`0x08000000`, with the board's 80 MHz clock configuration and FMC 1/1/2
timings. The baseline now initializes a Zephyr `k_heap` directly over the
external RW partition, obtains a 32-byte allocation (`0x60002034`), verifies
privileged read/write, and passes that allocated pointer to the unprivileged
worker through the external mailbox; the worker writes and reads it through
the RW MPU partition. The CP2102/UART4 console reported `worker user=1 result=0x5a PASS`.
MPU fault probes on the same 80 MHz setup produced these observations:

| Spike mode | External-SRAM result |
|---:|---|
| 0 | Worker executed from RX text at `0x60000000`, accessed its domain, and completed `worker user=1 result=0x5a PASS`. |
| 12 | RO write faulted at `0x60001000` (Data Access Violation, reason 19); worker fault was reported and terminated. |
| 3 | RW/XN execute faulted at `0x60002000` (Instruction Access Violation, reason 20); worker fault was reported and terminated. |
| 16 | Read of omitted 1 KiB region faulted at `0x60003000` (Data Access Violation, reason 19); worker fault was reported and terminated. |

These observations demonstrate target-side MPU execution/access behavior for
the tested domain layout. They do not qualify Zephyr thread stacks or DMA
buffers in external SRAM; those remain unsupported and must stay internal
unless separately validated. This spike is not MainApplication integration or
production release approval.

## WB55 USER_USB CDC lifecycle test (spike only)

Mode 7 adds a temporary host-owned CDC ACM test console on the P-NUCLEO-WB55
USER_USB connector. Build it with:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b nucleo_wb55rg application/application/ef800-isolation-spike -d application/build/ef800-isolation-spike-wb55-pm -- -DCONFIG_EF800_SPIKE_FAULT_MODE=7
```

The host application owns USB enumeration, CDC reads/writes, command parsing,
PluginManager package loading/unloading, and worker supervision. The dummy
plugin is built as an independent ARM ELF, packaged as `.pmp`, embedded as test
package bytes, then parsed, relocated, and descriptor-bound by PluginManager
at runtime into the RX/RO/RW plugin regions. The plugin's create, command, and
destroy functions are called by the unprivileged worker; it receives only the
bounded mailbox, not a USB device, UART, or driver handle. The worker trampoline
and CDC host remain in the spike application. The test uses the Zephyr
development VID/PID and must not be shipped as a product USB identity.

Mode 7 enables a 64 KiB common-libc malloc arena for PluginManager's manager
allocations. The current WB55 build reports 176,876 bytes of flash and 161,472
bytes of RAM used (about 82% of the board's configured RAM); this is test-only
and has not been qualified for the L496.

The host uses `pm_manager_create_instance_with_executor()` and
`pm_manager_destroy_instance_with_executor()` so PluginManager owns the active
instance record and service-table lifetime while the executor marshals each
lifecycle callback to the unprivileged worker. The executor rejects non-empty
service tables in this fixture rather than exposing privileged pointers. On
fault, the host first reaps the worker, then calls
`pm_manager_abandon_instance()` and unloads without invoking plugin destroy
code. A running `RESTART` gracefully destroys and unloads the current instance,
reloads its package, then starts a new generation.

Program only WB55 slot0 at `0x0800c000`, then launch directly through SWD by
setting VTOR/MSP/PC. Do not execute or program the bootloader. The ST-LINK
console remains on its existing COM port; USER_USB enumerates a separate CDC
COM port on the PC.

Send one newline-terminated command per line:

| Command | Expected result |
|---|---|
| `STATUS` | `STATUS STOPPED generation=0` |
| `START` | Starts a new unprivileged worker and returns `OK STARTED ... user=1`. |
| `PING hello` | Worker returns `ANSWER PING hello` through the host CDC service. |
| `CRASH` | Worker writes protected host RAM; host reports `PLUGIN_FAULT` and remains available. |
| `STATUS` after crash | Reports `CRASHED`; no worker remains active. |
| `RESTART` | Reaps/replaces the worker and returns `OK RESTARTED` with a newer generation. |
| `PING alive` | New worker returns `ANSWER PING alive`. |
| `FINISH` | Sends a graceful finish request, joins the worker, and returns `OK FINISHED`. |
| `RESTART` while running | Finishes the current worker, starts a fresh one, and advances generation. |

After connecting the USER_USB cable and identifying its CDC COM port, run the
automated host-side sequence:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test-isolation-usb-cdc.ps1 -PortName COM12
```

Replace `COM12` with the port enumerated for USER_USB; COM8 remains the ST-LINK
debug console and is not the test port.

WB55 bench result: USER_USB enumerated as COM21 while the ST-LINK console
remained COM8. The automated sequence passed, including `START`, command/reply,
graceful `FINISH`, restart while running, the deliberate dynamic-plugin crash
(`reason=19`, fault PC `0x20000066`, worker reap `0`, PluginManager abandon
`0`, unload `0`), and restart after the crash. The plugin reported `user=1`
after each start.
This remains WB55 spike evidence only, not L496 qualification.

This verifies dynamic package loading, PluginManager executor-backed lifecycle
state, and the USB-host/user-worker boundary on WB55. It is not the production
syscall gateway and does not qualify automatic recovery policy or L496
behavior.

## Flash

Use the matching probe and verify the detected MCU and image address before
flashing. For the NUCLEO-F767ZI, use its ST-LINK:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst -d "build\ef800-isolation-spike-f767\zephyr\zephyr.bin" 0x08000000 -rst
```

For EF800 L496 validation, only use the L496 probe and this L496 image. Verify
`Device ID : 0x461` and `Device name : STM32L496xx` before programming the image
at `0x08030000`; never flash this image to the L431 keyboard MCU.

For WB55 validation, use only the P-NUCLEO-WB55 probe and the WB55 image. Verify
the detected STM32WB55 device and its target supply before flashing at the
image's verified `0x0800c000` address. Do not flash a WB55 image to an EF800
controller.

## Fault-injection builds

The spike can inject one fault in the unprivileged worker by setting
`CONFIG_EF800_SPIKE_FAULT_MODE` to `1` through `6`:

| Value | Injection |
|---:|---|
| 1 | Read protected host RAM |
| 2 | Write protected host RAM |
| 3 | Execute from the RW/XN data partition |
| 4 | Execute an undefined Thumb instruction |
| 5 | Write directly into the worker's privileged-stack MPU guard |
| 6 | Deeply overflow the worker stack (exception-stacking stress case) |
| 8 | Trigger a privileged-thread Usage Fault (must not notify plugin supervisor) |
| 9 | Trigger a Usage Fault from a timer ISR (must not notify plugin supervisor) |
| 10 | Trigger a second fault while classifying a worker fault (nested fatal case) |
| 11 | Trigger a user-worker fault outside the registered plugin text range |

For example, build the read-fault case in a separate directory:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-isolation-spike -d application/build/ef800-isolation-spike-l496-read -- -DCONFIG_EF800_SPIKE_FAULT_MODE=1
```

The test fatal hook only returns for a thread-mode exception whose stacked PC
is inside the selected fault site in the known worker. Zephyr's
`z_fatal_error()` then aborts that faulting thread; the hook records only the
fatal reason and stacked PC and signals the supervisor. Any other fatal context
calls `k_fatal_halt()`. In the current Zephyr v4.4.2 source, expected reason
classes are `K_ERR_ARM_MEM_DATA_ACCESS` for protected data access,
`K_ERR_ARM_MEM_INSTRUCTION_ACCESS` for execute-never, and
`K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION` for UDF. Mode 5 directly writes the
known privileged-stack MPU guard from a valid worker instruction, isolating
guard permissions from recursive stack exhaustion. Mode 6 retains the deep
overflow case, which may fail while the CPU stacks the exception and therefore
may not have a recoverable stacked PC. `K_ERR_STACK_CHK_FAIL` is a distinct
stack sentinel/software-check reason. These are source-derived expectations,
not L496 test results. Hardware evidence is still required to confirm each
actual reason/PC and worker termination/notification behavior.

This remains a fault-injection spike, not production recovery code. Only
Zephyr v4.4.2 is installed in the current devcontainer, so the OpenSpec task
was updated to name the manifest revision being validated.

## Expected result

The ST-LINK console should contain output similar to:

```text
EF800 isolation spike
userspace=1 arm_mpu=1 xor_write=1
mpu_regions=<board-dependent> min_align=<board-dependent> stack_guard=1
worker user=1 result=0x5a PASS
```

On the NUCLEO-F767ZI, the auxiliary UART should receive:

```text
EF800 isolation auxiliary UART ready
```

The EF800 L496 build does not emit this auxiliary-UART message because USART1
is assigned as a measurement-port route; use COM7 for its spike output.

Successful normal-mode output demonstrates that the application reaches an
unprivileged worker and completes the memory-domain access path. It does not
prove EF800 package loading, MPU fault recovery, or external-flash behavior.

## External flash

The spike currently does not access the external flash. The NUCLEO-F767ZI board
DTS does not define a universal external-flash part or wiring. Before adding a
flash read/write gate, record the fitted flash manufacturer/part number, bus
(QSPI or SPI), chip-select, clock, IO pins, reset/write-protect wiring, and
whether the part is mounted on a shield or an adapter. The device-tree node and
JEDEC-compatible binding can then be added without guessing the hardware.
