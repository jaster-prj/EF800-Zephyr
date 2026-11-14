# Isolate External Plugin Execution with an Unprivileged Worker

- Status: accepted
- Date: 2026-10-06
- Supersedes: None

## Context

EF800 external packages are dynamically loaded into SRAM and currently execute through native function pointers on EF800 Application's privileged thread and stack. Contract validation and bounded data structures do not prevent package code from reading or corrupting host memory, accessing peripherals, blocking the UI, or taking EF800 Application through the system fatal path. Continuing an interrupted C call stack after a CPU exception cannot reliably restore locks, resource ownership, memory integrity, or peripheral state.

EF800 Application must retain the package-owned logical Measurement behavior established by ADR-0001 while enforcing its ownership of hardware, navigation, persistence, and resource lifetime. The STM32L496 MPU and Zephyr userspace provide a potential isolation boundary, but target MPU layout and fault-termination semantics must be proven before arbitrary external execution is enabled.

## Decision

Every external Measurement and Calculation package will execute on a dedicated unprivileged worker thread in a per-activation MPU domain. The domain will grant only guarded stack, RX plugin text, RO/XN plugin constants, RW/XN plugin state, and bounded shared-mailbox access. External packages will use a narrow verified syscall gateway and opaque capabilities for host services; privileged callback pointers, host objects, driver handles, peripheral mappings, and direct Zephyr access will not cross the boundary.

The privileged host will supervise each invocation with an absolute deadline and maintain a host-owned recovery ledger. A CPU exception may be recovered without reboot only when it is conclusively attributable to the active unprivileged worker and target evidence shows that terminating the worker is safe. Recovery abandons the faulted call stack, cleans up host-owned resources without invoking package teardown, quarantines that package for the boot, reports a host-rendered diagnostic, and returns through host navigation. Privileged, kernel, ISR, nested, service-handler, and ambiguous faults retain the normal fatal/reboot policy.

Packages that require the old direct-callback contract or cannot fit the MPU/W^X policy will be rejected before execution. The host will never weaken isolation as a compatibility fallback.

## Consequences

- Plugin UI and protocol behavior remains package-owned, while host resource ownership becomes hardware-enforced rather than contract-only.
- EF800 Application can remain available after a proven-contained plugin fault or timeout without resuming corrupted plugin frames.
- EF800 must enable Zephyr userspace, reserve worker stack and MPU resources, add a verified service gateway, and support host-only abandonment of faulted PluginManager instances.
- Existing external packages must be rebuilt against the isolated execution contract; old packages become explicitly incompatible.
- Fault containment, MPU permissions, deadlines, and escalation must be tested on STM32L496 because native simulation is insufficient.
- A failure in privileged service code or uncertain cleanup still causes controlled reboot.
- MPU isolation does not provide package authenticity, anti-rollback, storage qualification, or protection from physical/debug access; those remain separate release gates.
