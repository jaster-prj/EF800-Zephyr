## 1. Prove the Target Isolation Boundary

- [x] 1.1 Add a minimal STM32L496 target spike that enables Zephyr userspace, starts a non-essential unprivileged worker, and records the resulting flash, RAM, stack, and MPU-region budget.
- [x] 1.2 Demonstrate separate user RX text, RO/XN constants, RW/XN state, RW/XN mailbox, and guarded stack mappings without exposing host RAM, kernel objects, MMIO, or unrelated plugin memory.
- [ ] 1.3 Inject user-worker read, write, execute-never, invalid-instruction, and stack faults and document the exact Zephyr 4.4.1 fatal reasons and supported worker-termination/notification path.
- [ ] 1.4 Verify that privileged, ISR-context, nested, and unattributable fault injections retain fatal logging and controlled reboot, then remove any spike mechanism that broadly recovers system faults.

## 2. Define the Isolated Contract

- [ ] 2.1 Version the EF800 external package contract and metadata to declare isolated execution, memory requirements, target compatibility, and mediated service requirements.
- [ ] 2.2 Define the bounded invocation mailbox, operation identifiers, opaque capability tokens, generation fields, result records, and absolute-deadline representation without exposing Zephyr or host object types.
- [ ] 2.3 Replace public direct host-service callback/context pointers with SDK stubs for a narrow verified syscall gateway covering the services required by Measurement and Calculation packages.
- [ ] 2.4 Reject legacy direct-callback packages, invalid entry points, unsupported architectures, misaligned/oversized sections, and packages that cannot satisfy the MPU layout before executing package code.

## 3. Implement Memory Domains and Worker Supervision

- [ ] 3.1 Enable the required MainApplication userspace, MPU, stack-guard, thread, and fatal-diagnostic configuration and add an aligned isolation arena that preserves W^X permissions.
- [ ] 3.2 Implement the privileged loader mapping lifecycle for plugin text, rodata, data/BSS, mailbox, and user stack, including zeroization and generation changes before reuse.
- [ ] 3.3 Implement a dedicated unprivileged worker that performs external create, destroy, Measurement, Calculation, render, and input entry calls while the MainApplication UI thread remains schedulable.
- [ ] 3.4 Implement the privileged supervisor with serialized submission, generation-tagged completion, operation-specific absolute deadlines, timeout termination, and stale-response rejection.

## 4. Mediate Host Services

- [ ] 4.1 Implement syscall verification for active caller identity, privilege state, invocation generation, operation permission, mailbox offsets, direction, lengths, structure versions, capabilities, and remaining deadline.
- [ ] 4.2 Route valid Port requests through the existing host-owned assignment, claim, serial configuration, transport, and UART lifetime path without exposing transport or driver pointers to user mode.
- [ ] 4.3 Route valid workspace, clock, logging, UI, and input operations through bounded host-owned copies and reject unavailable or unauthorized capabilities without mutating host state.
- [ ] 4.4 Make blocking host operations respect the invocation's absolute deadline and add cancellation or peripheral-reset handling for UART operations used by Elta3 and FTS3XX.

## 5. Add Fault Classification and Recovery

- [ ] 5.1 Implement the minimal target-proven fault record and supervisor notification for an attributable active unprivileged plugin worker without logging, allocating, drawing, or mutating PluginManager state in fatal context.
- [ ] 5.2 Add a privileged recovery ledger that records package lifecycle, claimed route, pending service work, output ownership, memory-domain state, and cleanup progress independently of plugin memory.
- [ ] 5.3 Add a PluginManager host-only abandon operation that invalidates a faulted instance and unload bookkeeping without invoking package destroy code.
- [ ] 5.4 Implement ordered fault/timeout cleanup: stop dispatch, invalidate generation, cancel service work, reset/release routes, discard outputs, abandon the instance, remove mappings, and zero writable plugin memory.
- [ ] 5.5 Escalate to controlled reboot whenever fault attribution, worker termination, service cancellation, resource reset, manager abandonment, or cleanup verification cannot establish a known-safe state.

## 6. Integrate Quarantine and Navigation

- [ ] 6.1 Extend plugin registry diagnostics with fault and timeout classes and boot-scoped quarantine that rejects later activation of only the failed package.
- [ ] 6.2 Close the active host session after successful cleanup and post a host-owned recovery event containing plugin identity, operation, and failure class.
- [ ] 6.3 Add the LCD failure/timeout screen and acknowledgement flow that returns to the Measurement or Calculation parent menu without using plugin-provided display state.
- [ ] 6.4 Verify that healthy unrelated plugins remain available only after shared worker and resource state passes recovery checks, and that reboot clears runtime quarantine.

## 7. Migrate Production Packages

- [ ] 7.1 Rebuild the independent Elta3 and FTS3XX packages against the isolated SDK and route all port access through the mediated gateway while preserving current serial defaults and protocol behavior.
- [ ] 7.2 Move Calculation transformation and all Measurement create/destroy/render/input/measure calls off the MainApplication thread and remove the production direct function-pointer invocation fallback.
- [ ] 7.3 Preserve current bounded display, logical keyboard, measurement record, explicit workspace mutation, assignment, and missing/incompatible package behavior for successful invocations.

## 8. Verify and Close the Release Gate

- [ ] 8.1 Add native tests for contract/version rejection, syscall validation, generation handling, deadlines, lifecycle abandonment, cleanup ordering, quarantine, navigation events, and preservation of workspace data on failure.
- [ ] 8.2 Add target fault packages and STM32L496 tests for unauthorized read/write, XN execution, invalid instruction, stack fault, infinite loop, malformed service requests, stale generation, and faults during each package lifecycle phase.
- [ ] 8.3 Demonstrate on hardware that each recoverable plugin failure quarantines the package, restores UART/resource ownership, displays the diagnostic, returns to the correct menu, and leaves another valid plugin operational.
- [ ] 8.4 Demonstrate on hardware that host, privileged service, kernel, ISR, nested, and ambiguous faults reboot instead of being mislabeled as recovered plugin failures.
- [ ] 8.5 Run normal Elta3 and FTS3XX protocol/UI acceptance, Calculation/workspace tests, and the complete native test suite without weakening expected frames or bounded-result checks.
- [ ] 8.6 Pristine-build all four firmware targets, run `application/scripts/verify-images.sh`, inspect MainApplication map/RAM/stack growth, and confirm the fixed image contract still passes.
- [ ] 8.7 Update the plugin contract/security documentation and `docs/migration-status.md` with measured isolation limits, tested fault classes, timeout policy, remaining authenticity/storage gates, and the new native/target test baseline.
- [ ] 8.8 Keep arbitrary external executable packages release-disabled unless every MPU, fault, timeout, cleanup, normal-operation, build, image-layout, and hardware acceptance gate in this change passes.
