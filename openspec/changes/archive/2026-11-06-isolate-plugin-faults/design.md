## Context

The original EF800Application invoked external plugin callbacks synchronously on
its privileged thread. The isolated worker, syscall gateway, and fault recovery
path are now implemented and enabled in the standard physical EF800
EF800Application configuration. The physical build scans the normal
`Plugins` directory; only the separate recovery-test build scans synthetic
packages from Shared Data.

The STM32L496 target and Zephyr build support the ARM MPU and stack guards.
The physical EF800 configuration enables `CONFIG_USERSPACE` and the worker.
The remaining hardware and authenticity gaps still prevent treating the
development image as a production plugin release. The accepted package-owned
Measurement UI decision remains in force: packages own their bounded logical
behavior while EF800Application owns hardware, resource lifetime, raw
protocols, and global navigation.

The critical safety distinction is between abandoning an isolated worker and resuming a faulted call stack. Recovery is allowed only by the former. The design must also respect the fixed internal-flash layout and limited SRAM/MPU resources, and it must be proven on STM32L496 because `native_sim` cannot validate ARM exception or MPU behavior.

## Goals / Non-Goals

**Goals:**

- Run every external plugin entry point in an unprivileged, MPU-constrained execution domain.
- Keep EF800Application's UI/navigation thread alive and independently schedulable during plugin work.
- Mediate and validate every host-resource operation across the privilege boundary.
- Recover from attributable plugin-worker faults and timeouts by discarding the worker, cleaning up host-owned state, quarantining the package, and returning through host navigation.
- Preserve normal Elta3, FTS3XX, Measurement UI, Calculation, UART ownership, and workspace behavior for successful invocations.
- Retain fatal/reboot behavior for faults that are privileged, interrupt-context, kernel-related, nested, or not conclusively attributable to the plugin worker.
- Establish target evidence for MPU access control, W^X, stack protection, timeout handling, cleanup, and fault classification before enabling arbitrary external executable plugins.

**Non-Goals:**

- Resuming at the instruction after a faulting plugin call, editing the exception frame to skip instructions, or performing `longjmp`-style recovery from HardFault.
- Recovering EF800Application, kernel, ISR, privileged service-handler, or ambiguous faults.
- Protecting against denial of service that also defeats the hardware watchdog or against physical/debug access.
- Package signatures, anti-rollback, hot replacement, persistent automatic package disabling, or QSPI production qualification.
- Allowing plugins to create threads, access Zephyr APIs directly, map peripherals, access files, or perform unmediated workspace mutation.
- Isolating built-in trusted EF800Application code; the boundary applies to external executable packages.

## Decisions

### Use a supervised worker, not exception-frame continuation

The privileged host submits one bounded invocation descriptor to a dedicated plugin worker and waits asynchronously for completion while the main UI remains runnable. A single privileged supervisor thread serializes submissions; EF800Application submits a copied request and polls its token-tagged completion without waiting for plugin code. The worker executes the complete external lifecycle needed by that activation, including package create and destroy when healthy. A generation-tagged completion record distinguishes a valid result from a stale response after timeout or replacement. The current policy uses 30 seconds for create/measurement, 3 seconds for destroy, 60 seconds for calculation, and 1 second for interactive, render, and workspace callbacks; every worker call converts its policy limit to an absolute monotonic deadline and rejects late or mismatched generation/sequence responses.

Calculation snapshots are captured by the host and submitted asynchronously. The
worker's bounded output remains in host memory until the user explicitly
applies or discards it from a host-owned confirmation screen. Measurement
worker errors similarly produce a host-owned failure screen and return to the
menu that launched the request. These are software/UI paths; CPU fault delivery
and recovery still require the L496 validation gate.

If the worker faults or exceeds its deadline, the supervisor aborts or observes termination of that worker, invalidates the generation, and creates a fresh worker only after host cleanup and memory reinitialization. No code path returns through frames that were active at the fault.

```text
Main/UI thread               Privileged service path        User worker
      |                               |                         |
      |-- submit invocation ----------------------------------->|
      |   remain schedulable          |                         |
      |                               |<-- bounded syscall ------|
      |                               |-- validate/operate ------|
      |                               |-- bounded response ----->|
      |<------------------------- completion or fault notice ----|
      |                                                         x
      |-- cleanup -> quarantine -> LCD diagnostic -> parent menu
```

Alternatives considered:

- Patch the saved PC in the HardFault frame: rejected because it cannot restore C, lock, peripheral, or memory invariants and can repeat or conceal corruption.
- Use only a separate privileged thread: rejected as the final boundary because it protects the UI stack but not host or kernel memory.
- Reboot after every plugin fault: retained as the fallback, but unnecessarily discards healthy host state when MPU containment and cleanup are demonstrably complete.

### Use one unprivileged worker domain with per-activation MPU mappings

EF800Application enables Zephyr userspace and creates the plugin worker as a non-essential unprivileged thread with its own guarded stack. Before activation, privileged loader code validates and maps only:

- plugin text as user-readable and executable, not writable;
- plugin rodata as user-readable, execute-never, not writable;
- plugin data and BSS as user-readable/writable, execute-never;
- a small invocation mailbox/shared-buffer partition as user-readable/writable, execute-never;
- the worker stack through Zephyr's user-stack and guard mechanism.

Host RAM, other thread stacks, kernel objects, MMIO, package staging buffers, filesystem state, and inactive package memory are not granted. Plugin data is zeroed and the mailbox generation changed before reuse. Only one external package is active at a time, matching the current static image buffers and serialized host policy.

The current independently declared buffers are replaced or aligned as needed by a loader-owned isolation arena whose layout is representable by the STM32L496 MPU. The exact region budget, alignment, SRAM placement, and Zephyr memory-domain behavior are verified in an early target spike before worker activation is enabled on hardware. Software contract/loader work may proceed while that evidence is pending, but failure to represent RX/RO/RW/XN mappings rejects the package; it never causes a permissive fallback.

EF800Application enables `CONFIG_USERSPACE`, ARM MPU, execute-XOR-write,
hardware/MPU stack guards, thread monitoring, and exception diagnostics on the
physical EF800 composition. The static loader arena has 32 KiB RX text, 32 KiB
RO/XN constants, a 16 KiB RW/XN shared mailbox region with the 4 KiB plugin
data+BSS arena in its final aligned portion, and a 2 KiB guarded user stack.
The L496 MPU allows at most four dynamic domain partitions for this worker, so
the mailbox and plugin RW storage share one RW/XN partition. A 32-byte region
containing Zephyr's `z_arm_tls_ptr` is separately mapped privileged-RW/user-RO:
user-thread entry and PendSV require that TLS pointer. The MPU-aligned window
also includes adjacent libc globals, but the remaining libc partition,
including the allocator mutex, is not mapped to the worker. User code cannot
write the exposed window. The linker map and target MPU behavior confirm these
alignments and permissions.

The standard physical EF800Application now enables the isolated worker, scans
up to 16 normal Plugins-directory entries, and bounds PluginManager to the
two independently packaged production instruments. Its 16 KiB malloc arena
can stage their verified 14,444-byte and 13,284-byte packages. The build uses
247,744 of 262,144 bytes of RAM (94.5%), produces a 373,108-byte `zephyr.bin`,
and links at `0x08030000`. On the L496 it booted, started the watchdog, and
discovered plugin IDs 2 and 3. No measurement instrument was available, so no
UART-dependent plugin operation was exercised in this configuration.

The separate recovery-test image uses a test-only Shared Data directory, a
nine-entry discovery/descriptor table, and two loaded-manager slots; it uses
255,936 of 262,144 bytes of RAM (97.6%) and produces a 373,216-byte
`zephyr.bin`. Its one-second Calculation deadline and synthetic packages do not
apply to the standard EF800Application.
The recovery-test linker map places the 2 KiB plugin worker stack at
`0x20039000`; Zephyr reserves MPU-aligned user-stack space for it and the
4 KiB privileged supervisor stack. The image verifier passes the fixed
bootloader, backup, metadata, main, and keyboard boundaries after the four
pristine firmware builds.

Alternatives considered:

- Map the whole plugin arena RWX: rejected because a memory bug could modify executable code and bypass package validation.
- Create one permanent domain per installed package: rejected because execution is serialized and permanent mappings consume scarce MPU/kernel resources.
- Copy plugin code into internal flash: rejected for this change because it changes installation, update, and flash-layout policy.

### Cross the boundary through a narrow syscall gateway

Plugins no longer receive privileged transport pointers or callbacks that execute host adapters directly. The public isolated-execution contract exposes unprivileged-callable SDK stubs and opaque scalar capability tokens. A stub enters one EF800 syscall gateway with an operation code and offsets/lengths into the granted mailbox. Privileged verification code checks:

- the caller is the active plugin worker and generation;
- the operation is allowed for the active plugin kind and binding;
- all ranges are within the granted mailbox, correctly directed, and bounded;
- lengths, timeouts, enums, and structure versions are valid;
- the claimed UART/workspace/UI capability is still owned by the host session;
- the request and its blocking work fit the remaining invocation deadline.

Privileged service execution copies inputs into host-owned storage before use and copies validated outputs back only within the declared range. Driver handles, Zephyr objects, host context pointers, workspace objects, and MMIO addresses never cross into user memory. Port reads and writes continue to run through EF800Application's existing host-owned route and transport policy.

The mailbox ABI is fixed-width and pointer-free. It carries a generation and sequence, a bounded operation code, mailbox-relative input/output ranges, an absolute monotonic deadline, and a session-scoped opaque capability. The operation verifier applies per-service size limits (including the existing 128-byte serial transfer bound) before touching payload bytes. Host-to-worker lifecycle, measurement, calculation, render, input, and workspace-record callbacks are explicit worker invocations; UI display and workspace mutation remain host-owned copies/decisions rather than plugin callbacks into host objects.

Installed plugins may each have a host-side port assignment. On activation the host resolves that assignment, claims the route, and issues a capability scoped to the current plugin and generation; the plugin cannot select a physical port by supplying an ID. There is one worker and at most one active plugin session, so switching plugins closes/releases the current session before mapping and activating another. Paused concurrent plugin execution is not part of this interface.

The package contract is versioned so packages using the old direct callback service table are rejected before any package entry point executes. Elta3 and FTS3XX are rebuilt against the isolated SDK together with the host; no compatibility shim executes old packages with reduced protection.

Alternatives considered:

- Retain service vtables with privileged function pointers: rejected because direct branches do not provide pointer verification or controlled privilege transition.
- Give each service a separate syscall: viable, but a single versioned gateway keeps dynamic-package imports and verifier surface small.
- Use only queues with no syscall: still requires a safe kernel transition or granted kernel objects and complicates caller authentication; the mailbox may be used for payload transport behind the gateway.

### Attribute and recover faults conservatively

The target spike first establishes the supported Zephyr path for observing and terminating an unprivileged user-thread CPU exception without overriding unrelated fatal behavior. Recovery classification requires all of the following:

- the fault occurred in thread context, not ISR context;
- the current thread is the registered active plugin worker;
- the worker was unprivileged and inside an active invocation generation;
- the exception/fatal reason is in the explicitly tested recoverable set;
- no nested fault, kernel panic, privileged service fault, or supervisor-integrity failure is present.

The exception frame is never resumed. The fault path writes only a minimal preallocated record and signals the supervisor using a mechanism proven safe for that Zephyr fatal context. Logging, LCD drawing, UART reset, PluginManager mutation, unload, and allocation happen later in ordinary privileged thread context.

If any predicate is false, or Zephyr cannot support the required plugin-thread termination semantics on this target, the normal fatal/reboot policy wins. The implementation must not install a broad `k_sys_fatal_error_handler` that returns from all CPU exceptions.

### Make cleanup host-owned and ordered

Each invocation has a privileged recovery ledger recording host-owned resources and completed lifecycle transitions. Fault cleanup uses the ledger rather than plugin memory:

EF800Application now keeps a host-only ledger snapshot with package and manager
IDs, active lifecycle state, claimed port/connection and privileged transport
binding, worker generation/domain state, pending operation and service,
deadline, host-owned output capacity, last status, and a cleanup-step bitset.
The gateway updates service-pending state around dispatch; only a copied
privileged snapshot API exposes the ledger to host diagnostics and recovery.
The ledger is independent of plugin RW memory and remains available when
worker state is being invalidated.

```text
stop accepting service requests
  -> invalidate invocation generation
  -> cancel/wake bounded host service work
  -> reset and release claimed UART/route
  -> discard output and pending workspace mutation
  -> invalidate manager instance without plugin destroy callback
  -> remove plugin memory mappings
  -> zero writable plugin memory and mailbox
  -> mark registry entry quarantined
  -> publish host diagnostic/navigation event
```

Normal closure still calls plugin destroy in the worker before releasing resources. Fault and timeout closure must provide a PluginManager host-side abandon operation that drops bookkeeping without entering package code. If a driver operation cannot be cancelled, cleanup waits only within a separately defined recovery bound and reboots if the route cannot be restored to a known-safe state.

The fatal handler records the supported reason, stacked PC, and active worker
generation in a preallocated atomic record, then wakes the privileged
supervisor. It accepts only thread-mode faults from the active `K_USER` worker
with a PC inside its mapped plugin regions and an L496-tested MPU/instruction/
undefined-instruction reason. All other fatal contexts retain the non-recovery
controlled-reboot path after Zephyr's fatal diagnostic. Cleanup failures are
also escalated by EF800Application to `sys_reboot(SYS_REBOOT_COLD)`.

The target Calculation fault tests exposed two recovery defects that are now
fixed: the custom domain initially omitted the TLS pointer needed by Zephyr's
user-thread entry, and the post-detach recovery ledger retained
`worker_mapped=true`, preventing manager unload. The TLS mapping is now
privileged-RW/user-RO, shares the four-region L496 domain budget with the
mailbox/RW region, and recovery clears the ledger mapping state after detach.
On the L496, invalid-instruction plugin 10 faulted at `0x20018010`; unauthorized
read plugin 12 faulted at `0x20000000`; unauthorized-write plugin 13 faulted at
the same address; and XN plugin 14 faulted while fetching from RW plugin memory
at `0x20023800`. Each was attributed to its package, cleaned up, and
quarantined. Infinite-loop plugin 16 exceeded the test-only 1-second Calculation
deadline and was also quarantined. Create-fault plugin 17 faulted at
`0x2001800e` before publishing an instance; destroy-fault plugin 18 faulted at
`0x2001807a` with an active instance. Both lifecycle faults were abandoned and
unloaded without package teardown. Healthy plugin 11 completed with a 19-byte
result after recovery; applying it appended `healthy calculation` to the
selected test Workspace, while a later discard left the Workspace unchanged.
Reboot rediscovered the test packages and reset runtime quarantine.

Test-only Calculation plugins 19 and 20 sent direct SVC requests from their
create callbacks. Plugin 19 set the reserved header bit; plugin 20 supplied a
non-active generation. The STM32L496 gateway rejected both with `-EINVAL`
without dispatching the service. Each fixture then sent a valid log request at
the same sequence number, which succeeded, and completed its Calculation
invocation without quarantine.

Stack-overflow plugin 15 raised Zephyr fatal reason 2 with a stacking error and
stacked PC `0x00000000`; the conservative PC predicate correctly rejected
plugin attribution and used controlled reboot. That reboot exposed an IWDG
adoption gap (`Watchdog start failed: -19`). EF800Application now adopts and
directly feeds the bootloader-started IWDG when the device driver is not ready
or reports `-ENODEV` while installing the timeout; the repeated stack-fault
reboot and a separate software reboot both returned to a stable EF800Application
boot with the watchdog task active. Measurement
lifecycle faults, UART route recovery, privileged/ISR/nested fault injection,
and normal hardware protocol acceptance remain open.

Native regression coverage for this change passes 25/25 Twister scenarios
(164/164 cases), 6/6 measurement-plugin CTests, and 3/3 PluginManager CTests.
It covers worker-message bounds/generations/deadlines, package protocols and
native package loading, instance abandonment, registry quarantine/reset,
recovery-ledger generation clearing, UI/session navigation, cleanup-failure
escalation, and rejection of partial Calculation output. These model and unit
tests do not replace the remaining STM32L496 fault and UART gates.

`docs/plugin-contract/security-policy.md` is the current plugin release-gate
and bring-up evidence document. The older `docs/migration-status.md` is
obsolete relative to the completed migration and remains deleted; it is not a
release or closeout gate.

The software supervisor applies this ordered sequence for attributable worker
faults and timeouts: stop worker dispatch, reap the worker, unbind the gateway
with a bounded wait, reset and release its claimed UART through the host
route-recovery callback, zero and discard pending outputs, abandon the
PluginManager instance without plugin teardown, remove the memory-domain
mapping, and unload to zero plugin text, rodata, RW, BSS, mailbox, and stack
storage. The L496 Calculation fault tests verified the no-route cleanup path,
boot-scoped quarantine, malformed/stale service rejection, repeated worker
reuse, and subsequent healthy Calculation execution. UART route recovery and
the Measurement fault matrix remain unverified in tasks 8.3–8.5.

### Quarantine is boot-scoped and package-specific

A fault or timeout records plugin ID, operation, failure class, worker generation,
and available architecture status in the host-owned completion record. After
cleanup succeeds, EF800Application marks only that registry entry `FAULTED` or
`TIMED_OUT`; host activation checks and menu counts ignore non-ready entries.
The registry is RAM-only and is initialized again on boot, so quarantine is
boot-scoped. Other packages remain usable only after all shared resources and
the worker environment have passed cleanup. Runtime quarantine is not persisted
as a package ban because power loss, package replacement, and diagnostic
retention require a separate policy; a bounded persistent last-failure
diagnostic may be added without changing activation policy.

### Use a host-owned recovery screen and navigation event

The supervisor posts a failure event to EF800Application rather than touching LCD/menu state from fatal context. EF800Application closes its logical plugin session, renders a bounded host-owned message containing plugin name and fault/timeout class, and returns to the Measurement or Calculation parent after acknowledgement. A failed cleanup posts a reboot event instead. Plugin-provided display content is never used after the fault.

### Apply operation-specific deadlines

Every worker request carries an absolute host deadline. Short metadata, input, and render calls use short limits; Measurement and bounded port reads may use longer policy-defined limits; Calculation uses a bounded compute limit. A mediated service cannot extend the absolute deadline. Timeout policy and values are centralized, diagnosable, and covered by target tests rather than embedded in plugin packages.

The host UART adapter uses an absolute-deadline read loop and interrupt-driven
transmit instead of the legacy unbounded `uart_poll_out` path for worker Port
requests. A transmit timeout disables further TX interrupts and reapplies the
claimed route's serial configuration before releasing the staging buffer. The
transport is then marked unavailable because a partial command may already
have reached the instrument; it is not reused until host route recovery
reinitializes it. This is a software implementation result only and still
requires L496 normal-protocol and cancellation validation.

The hardware watchdog remains a final system recovery layer, not the normal plugin timeout mechanism. The supervisor must remain schedulable at a priority that a user worker cannot starve.

## Risks / Trade-offs

- [STM32L496 MPU regions or alignment cannot represent the desired mappings alongside Zephyr guards] -> Prove the complete region budget and generated MPU configuration on target first; reduce arena granularity or reject oversized packages, never merge writable and executable permissions.
- [Zephyr's target/version fatal path cannot safely hand a user-thread CPU exception back to a supervisor] -> Verify with a minimal target spike and retain reboot as the fallback; do not emulate recovery by returning from HardFault.
- [A fault occurs while privileged service code operates on behalf of a plugin] -> Classify it as a host fault and reboot, because MPU isolation cannot prove privileged code remained coherent.
- [Aborting a worker leaves PluginManager lifecycle bookkeeping inconsistent] -> Add an explicit host-only abandon path and test every lifecycle phase; never call package destroy after fault.
- [A blocked UART operation delays timeout cleanup] -> Propagate absolute deadlines, use cancellable/bounded transport operations, reset hardware during cleanup, and reboot if ownership cannot be proven safe.
- [Userspace/syscall and extra stacks increase flash/RAM usage] -> Measure pristine build/map deltas against the fixed image contract before completing the change.
- [One worker serializes plugins] -> Accept this because EF800Application already serializes plugin/resource ownership and the LCD supports one active session; preserve UI responsiveness through asynchronous supervision.
- [Old packages stop loading] -> Version the isolation contract, report incompatibility clearly, and rebuild the independently packaged Elta3 and FTS3XX artifacts with the matching SDK.
- [MPU containment is mistaken for package authenticity] -> Keep signature, anti-rollback, and storage qualification gates explicitly separate and continue release-disabling arbitrary packages until all relevant gates pass.

## Migration Plan

1. Build a target-only isolation spike that proves userspace entry, RX/RO/RW/XN mappings, guarded stack behavior, worker-only fault termination, conservative escalation, and MPU region budget on STM32L496.
2. Add the versioned isolated-execution contract and syscall gateway while external execution remains release-disabled; add verifier tests for caller, generation, operation, range, and deadline checks.
3. Refactor the host into supervisor, worker, mailbox, recovery ledger, and PluginManager abandon lifecycle, retaining a compile-time test path until package migration is complete.
4. Rebuild Elta3 and FTS3XX against the new SDK and move all create/destroy/render/input/measure/calculate calls into the worker. Remove the production direct-call path rather than retaining a fallback.
5. Integrate boot-scoped registry quarantine, diagnostics, LCD recovery, parent-menu navigation, and resource-reset escalation.
6. Run native contract/lifecycle tests, malicious/faulting target packages, normal hardware protocol tests, all four pristine firmware builds, and image/layout verification. Keep arbitrary package execution release-disabled until every gate passes.

Rollback during development disables external package activation and restores the last known-good EF800Application image through the existing boot/update process. Runtime rollback after a contained fault means package quarantine for that boot; uncertainty in host integrity causes controlled reboot, not continued operation.

## Open Questions

- Which exact Zephyr v4.4.2 API and fatal reason sequence provides supported notification when an unprivileged dynamic-code worker faults on ARM Cortex-M4, and what minimal hook is required? This must be answered by the target spike before production structure is fixed.
- How many STM32L496 MPU regions remain available after Zephyr userspace, stack guards, and board mappings, and what arena sizes/alignment fit without weakening W^X?
- What operation-specific deadlines preserve Elta3 at 1200 7O2 and FTS3XX at 9600 8N1 under worst-case valid instrument latency?
- Which current UART transport waits are cancellable, and which require peripheral reset during recovery?
- Should the bounded last-failure diagnostic survive reboot, or should persistence be deferred to a separate diagnostics policy?
