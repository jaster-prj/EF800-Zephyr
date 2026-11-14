## Why

External plugins currently execute synchronously on EF800 Application's privileged thread, stack, and address space, so a plugin CPU fault enters the system fatal path and can also corrupt host state before the fault. EF800 Application needs a defined containment boundary that can reject unauthorized access and recover from a fault in external plugin code without pretending that the interrupted C call stack is safe to resume.

## What Changes

- Execute external Measurement and Calculation plugin code in a dedicated unprivileged worker with an MPU-constrained stack and plugin memory regions.
- Replace direct plugin-to-host callbacks with bounded, validated requests mediated by privileged host code.
- Classify faults by execution context so recoverable plugin-worker faults terminate only the plugin execution, while host, kernel, interrupt, and unclassifiable faults retain the normal fatal/reboot policy.
- Add host-owned recovery that releases plugin resources without calling faulted code, marks the plugin unavailable for the rest of the boot, reports a diagnostic on the LCD, and returns to the appropriate host menu.
- Add bounded completion and timeout supervision for plugin invocations so a non-returning plugin cannot indefinitely block the EF800 Application UI.
- Preserve the current plugin ABI's user-visible Measurement and Calculation behavior where possible, but require external packages to use the isolated execution contract before they can run.

## Capabilities

### New Capabilities

- `plugin-execution-isolation`: Defines unprivileged MPU isolation, mediated host services, invocation supervision, fault classification, cleanup, boot-scoped quarantine, LCD diagnostics, and menu recovery for external plugin execution.

### Modified Capabilities

None.

## Impact

- EF800 Application plugin host, plugin session lifecycle, menu recovery, registry diagnostics, LCD status handling, and fatal-error integration.
- PluginManager Zephyr/ARM loading and invocation integration, including executable/data memory placement and user-mode entry constraints.
- EF800 plugin SDK and Port service contract, because raw privileged callback pointers cannot remain the isolation boundary.
- Zephyr configuration for userspace, MPU domains, guarded worker stacks, thread supervision, and fault reporting.
- Measurement and Calculation packages, which must remain independent packages but execute through the isolated worker and mediated service path.
- Native tests, target-only MPU/fault-injection tests, all firmware builds, image-size/layout verification, and hardware validation on STM32L496.
