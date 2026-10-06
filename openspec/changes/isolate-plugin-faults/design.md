## Context

MainApplication currently allocates one static set of plugin text, rodata, data, and BSS buffers, binds package entry addresses into native function pointers, and invokes create, destroy, Measurement, Calculation, input, and render callbacks synchronously. Those callbacks run on the caller's privileged thread and stack. The current Port service is another function-pointer table whose callbacks dereference a host transport pointer. ABI bounds protect serialization but do not create a privilege or memory boundary.

The STM32L496 target and current Zephyr build support the ARM MPU and stack guards, but `CONFIG_USERSPACE` is disabled. The production plugin integration explicitly defers MPU/W^X, stack, timeout, and fault behavior, and arbitrary external executable plugins remain release-disabled pending that evidence. The accepted package-owned Measurement UI decision remains in force: packages own their bounded logical behavior while MainApplication owns hardware, resource lifetime, raw protocols, and global navigation. This design strengthens that ownership boundary without reversing it.

The critical safety distinction is between abandoning an isolated worker and resuming a faulted call stack. Recovery is allowed only by the former. The design must also respect the fixed internal-flash layout and limited SRAM/MPU resources, and it must be proven on STM32L496 because `native_sim` cannot validate ARM exception or MPU behavior.

## Goals / Non-Goals

**Goals:**

- Run every external plugin entry point in an unprivileged, MPU-constrained execution domain.
- Keep MainApplication's UI/navigation thread alive and independently schedulable during plugin work.
- Mediate and validate every host-resource operation across the privilege boundary.
- Recover from attributable plugin-worker faults and timeouts by discarding the worker, cleaning up host-owned state, quarantining the package, and returning through host navigation.
- Preserve normal Elta3, Fennel1, Measurement UI, Calculation, UART ownership, and workspace behavior for successful invocations.
- Retain fatal/reboot behavior for faults that are privileged, interrupt-context, kernel-related, nested, or not conclusively attributable to the plugin worker.
- Establish target evidence for MPU access control, W^X, stack protection, timeout handling, cleanup, and fault classification before enabling arbitrary external executable plugins.

**Non-Goals:**

- Resuming at the instruction after a faulting plugin call, editing the exception frame to skip instructions, or performing `longjmp`-style recovery from HardFault.
- Recovering MainApplication, kernel, ISR, privileged service-handler, or ambiguous faults.
- Protecting against denial of service that also defeats the hardware watchdog or against physical/debug access.
- Package signatures, anti-rollback, hot replacement, persistent automatic package disabling, or QSPI production qualification.
- Allowing plugins to create threads, access Zephyr APIs directly, map peripherals, access files, or perform unmediated workspace mutation.
- Isolating built-in trusted MainApplication code; the boundary applies to external executable packages.

## Decisions

### Use a supervised worker, not exception-frame continuation

The privileged host submits one bounded invocation descriptor to a dedicated plugin worker and waits asynchronously for completion while the main UI remains runnable. The worker executes the complete external lifecycle needed by that activation, including package create and destroy when healthy. A generation-tagged completion record distinguishes a valid result from a stale response after timeout or replacement.

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

MainApplication enables Zephyr userspace and creates the plugin worker as a non-essential unprivileged thread with its own guarded stack. Before activation, privileged loader code validates and maps only:

- plugin text as user-readable and executable, not writable;
- plugin rodata as user-readable, execute-never, not writable;
- plugin data and BSS as user-readable/writable, execute-never;
- a small invocation mailbox/shared-buffer partition as user-readable/writable, execute-never;
- the worker stack through Zephyr's user-stack and guard mechanism.

Host RAM, other thread stacks, kernel objects, MMIO, package staging buffers, filesystem state, and inactive package memory are not granted. Plugin data is zeroed and the mailbox generation changed before reuse. Only one external package is active at a time, matching the current static image buffers and serialized host policy.

The current independently declared buffers are replaced or aligned as needed by a loader-owned isolation arena whose layout is representable by the STM32L496 MPU. The exact region budget, alignment, SRAM placement, and Zephyr memory-domain behavior are verified in an early target spike before the package ABI migration proceeds. Failure to represent RX/RO/RW/XN mappings rejects the package; it never causes a permissive fallback.

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

Privileged service execution copies inputs into host-owned storage before use and copies validated outputs back only within the declared range. Driver handles, Zephyr objects, host context pointers, workspace objects, and MMIO addresses never cross into user memory. Port reads and writes continue to run through MainApplication's existing host-owned route and transport policy.

The package contract is versioned so packages using the old direct callback service table are rejected before any package entry point executes. Elta3 and Fennel1 are rebuilt against the isolated SDK together with the host; no compatibility shim executes old packages with reduced protection.

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

### Quarantine is boot-scoped and package-specific

A fault or timeout records plugin ID, operation, failure class, and available architecture status in host-owned diagnostics, then marks that registry entry unavailable until reboot. Other packages remain usable only after all shared resources and the worker environment have passed cleanup. Runtime quarantine is not persisted as a package ban because power loss, package replacement, and diagnostic retention require a separate policy; a bounded persistent last-failure diagnostic may be added without changing activation policy.

### Use a host-owned recovery screen and navigation event

The supervisor posts a failure event to MainApplication rather than touching LCD/menu state from fatal context. MainApplication closes its logical plugin session, renders a bounded host-owned message containing plugin name and fault/timeout class, and returns to the Measurement or Calculation parent after acknowledgement. A failed cleanup posts a reboot event instead. Plugin-provided display content is never used after the fault.

### Apply operation-specific deadlines

Every worker request carries an absolute host deadline. Short metadata, input, and render calls use short limits; Measurement and bounded port reads may use longer policy-defined limits; Calculation uses a bounded compute limit. A mediated service cannot extend the absolute deadline. Timeout policy and values are centralized, diagnosable, and covered by target tests rather than embedded in plugin packages.

The hardware watchdog remains a final system recovery layer, not the normal plugin timeout mechanism. The supervisor must remain schedulable at a priority that a user worker cannot starve.

## Risks / Trade-offs

- [STM32L496 MPU regions or alignment cannot represent the desired mappings alongside Zephyr guards] -> Prove the complete region budget and generated MPU configuration on target first; reduce arena granularity or reject oversized packages, never merge writable and executable permissions.
- [Zephyr's target/version fatal path cannot safely hand a user-thread CPU exception back to a supervisor] -> Verify with a minimal target spike and retain reboot as the fallback; do not emulate recovery by returning from HardFault.
- [A fault occurs while privileged service code operates on behalf of a plugin] -> Classify it as a host fault and reboot, because MPU isolation cannot prove privileged code remained coherent.
- [Aborting a worker leaves PluginManager lifecycle bookkeeping inconsistent] -> Add an explicit host-only abandon path and test every lifecycle phase; never call package destroy after fault.
- [A blocked UART operation delays timeout cleanup] -> Propagate absolute deadlines, use cancellable/bounded transport operations, reset hardware during cleanup, and reboot if ownership cannot be proven safe.
- [Userspace/syscall and extra stacks increase flash/RAM usage] -> Measure pristine build/map deltas against the fixed image contract before completing the change.
- [One worker serializes plugins] -> Accept this because MainApplication already serializes plugin/resource ownership and the LCD supports one active session; preserve UI responsiveness through asynchronous supervision.
- [Old packages stop loading] -> Version the isolation contract, report incompatibility clearly, and rebuild the independently packaged Elta3 and Fennel1 artifacts with the matching SDK.
- [MPU containment is mistaken for package authenticity] -> Keep signature, anti-rollback, and storage qualification gates explicitly separate and continue release-disabling arbitrary packages until all relevant gates pass.

## Migration Plan

1. Build a target-only isolation spike that proves userspace entry, RX/RO/RW/XN mappings, guarded stack behavior, worker-only fault termination, conservative escalation, and MPU region budget on STM32L496.
2. Add the versioned isolated-execution contract and syscall gateway while external execution remains release-disabled; add verifier tests for caller, generation, operation, range, and deadline checks.
3. Refactor the host into supervisor, worker, mailbox, recovery ledger, and PluginManager abandon lifecycle, retaining a compile-time test path until package migration is complete.
4. Rebuild Elta3 and Fennel1 against the new SDK and move all create/destroy/render/input/measure/calculate calls into the worker. Remove the production direct-call path rather than retaining a fallback.
5. Integrate boot-scoped registry quarantine, diagnostics, LCD recovery, parent-menu navigation, and resource-reset escalation.
6. Run native contract/lifecycle tests, malicious/faulting target packages, normal hardware protocol tests, all four pristine firmware builds, and image/layout verification. Keep arbitrary package execution release-disabled until every gate passes.

Rollback during development disables external package activation and restores the last known-good MainApplication image through the existing boot/update process. Runtime rollback after a contained fault means package quarantine for that boot; uncertainty in host integrity causes controlled reboot, not continued operation.

## Open Questions

- Which exact Zephyr 4.4.1 API and fatal reason sequence provides supported notification when an unprivileged dynamic-code worker faults on ARM Cortex-M4, and what minimal hook is required? This must be answered by the target spike before production structure is fixed.
- How many STM32L496 MPU regions remain available after Zephyr userspace, stack guards, and board mappings, and what arena sizes/alignment fit without weakening W^X?
- What operation-specific deadlines preserve Elta3 at 1200 7O2 and Fennel1 at 9600 8N1 under worst-case valid instrument latency?
- Which current UART transport waits are cancellable, and which require peripheral reset during recovery?
- Should the bounded last-failure diagnostic survive reboot, or should persistence be deferred to a separate diagnostics policy?
