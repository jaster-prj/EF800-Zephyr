# plugin-execution-isolation Specification

## Purpose

TBD: Define isolated execution, supervision, and recovery for external plugins.

## Requirements

### Requirement: Execute external plugins in an unprivileged isolation domain
EF800Application SHALL execute every external Measurement and Calculation plugin entry point on a dedicated unprivileged worker thread whose MPU domain grants access only to its guarded stack, the active plugin's executable and data regions, and explicitly shared invocation buffers. EF800Application, kernel, peripheral, and unrelated plugin memory SHALL remain inaccessible to the plugin worker.

#### Scenario: External plugin is invoked
- **WHEN** EF800Application invokes a validated external Measurement or Calculation plugin
- **THEN** all plugin lifecycle and operation entry points execute on the unprivileged plugin worker rather than the EF800Application thread

#### Scenario: Plugin accesses unauthorized host memory
- **WHEN** an external plugin attempts to read, write, or execute memory outside its granted MPU regions
- **THEN** the processor raises a plugin-worker fault and the unauthorized access does not modify the protected host resource

#### Scenario: Plugin data is non-executable
- **WHEN** an external plugin attempts to execute code from its writable data, BSS, stack, or shared-buffer region
- **THEN** the MPU denies execution and raises a plugin-worker fault

### Requirement: Mediate plugin access to host services
External plugins SHALL access UART, workspace, clock, logging, display, keyboard, and other host-owned resources only through bounded requests validated and executed by privileged host code. An external plugin SHALL NOT receive a privileged object pointer, driver handle, peripheral address, or callback that bypasses mediation.

#### Scenario: Plugin performs a permitted port operation
- **WHEN** the active Measurement plugin submits a valid bounded read or write request for its assigned host-owned port
- **THEN** privileged host code validates the request, performs the operation against that binding, and returns only the bounded result to the plugin worker

#### Scenario: Plugin submits an invalid service request
- **WHEN** a plugin supplies an unknown operation, invalid length, out-of-domain buffer, unavailable capability, or request inconsistent with its active binding
- **THEN** the host rejects the request without accessing the requested resource or corrupting host state

#### Scenario: Plugin attempts direct peripheral access
- **WHEN** an external plugin dereferences a peripheral address instead of using a granted service
- **THEN** MPU enforcement faults the plugin worker and EF800Application retains ownership of the peripheral

### Requirement: Supervise every plugin invocation
EF800Application SHALL assign a finite host-defined deadline to every external plugin lifecycle, Measurement, Calculation, input, render, and service operation. The host UI SHALL remain independently schedulable while a plugin invocation is running.

#### Scenario: Plugin completes within its deadline
- **WHEN** an external plugin invocation completes before its configured deadline with a valid result
- **THEN** EF800Application validates the result and continues the existing Measurement or Calculation workflow

#### Scenario: Plugin does not return
- **WHEN** an external plugin invocation exceeds its configured deadline
- **THEN** EF800Application terminates the plugin worker, treats the invocation as failed, and begins host-owned recovery without waiting indefinitely

#### Scenario: Plugin repeatedly requests host work
- **WHEN** a plugin uses mediated services during an invocation
- **THEN** service handling remains bounded by the invocation deadline and cannot indefinitely extend the operation

### Requirement: Recover only faults attributable to the plugin worker
The fatal-error policy SHALL classify a recoverable plugin fault only when the fault occurred in thread context on the active unprivileged plugin worker and the recorded execution state is sufficient to attribute it to that worker. EF800Application SHALL NOT resume the interrupted plugin call stack or skip the faulting instruction.

#### Scenario: Plugin worker raises a CPU exception
- **WHEN** the active unprivileged plugin worker causes an MPU, instruction, bus, usage, or stack-protection fault that is confined and attributable to that worker
- **THEN** the system terminates that worker and notifies the host supervisor without resuming its exception frame

#### Scenario: Host or kernel code faults
- **WHEN** a fault occurs in EF800Application, privileged host-service code, kernel code, interrupt context, or an execution context that cannot be conclusively attributed to the plugin worker
- **THEN** the system retains the normal fatal logging and reboot policy and SHALL NOT report the event as a recovered plugin failure

#### Scenario: Fault handling faults again
- **WHEN** a nested or repeated fatal exception prevents reliable plugin-only recovery
- **THEN** the system follows the normal fatal/reboot policy

### Requirement: Perform host-owned cleanup after plugin failure
After a plugin fault or timeout, EF800Application SHALL stop plugin dispatch, release or reset every host-owned resource associated with the invocation, invalidate the plugin instance without invoking faulted plugin teardown code, and leave the plugin execution environment reusable only after it has been reinitialized.

#### Scenario: Measurement plugin faults while owning a route
- **WHEN** an active Measurement plugin faults while its host-owned UART route is claimed
- **THEN** EF800Application cancels pending service work, resets and releases the route, clears the active plugin session, and does not call the plugin destructor

#### Scenario: Calculation plugin faults with pending output
- **WHEN** a Calculation plugin faults before its result is accepted
- **THEN** EF800Application discards the untrusted partial output and performs no workspace mutation

#### Scenario: Cleanup cannot restore a resource
- **WHEN** host-owned recovery cannot establish a known-safe state for a claimed resource
- **THEN** EF800Application escalates to the normal controlled reboot policy rather than returning to the menu with uncertain ownership

### Requirement: Quarantine a failed plugin for the current boot
EF800Application SHALL mark an external plugin that faults or times out as unavailable for the remainder of the current boot, preserve a diagnosable failure reason, and reject subsequent activation attempts without executing that package again. Other valid plugins SHALL remain available when host integrity and shared resources remain known-safe.

#### Scenario: User selects a quarantined plugin
- **WHEN** the user attempts to activate a plugin that faulted earlier in the current boot
- **THEN** EF800Application reports that the plugin is unavailable and does not load or execute it

#### Scenario: Another plugin remains healthy
- **WHEN** one external plugin is quarantined and recovery restored all shared host resources to a known-safe state
- **THEN** unrelated ready plugins remain selectable and executable

#### Scenario: Device reboots after a plugin failure
- **WHEN** EF800Application starts a new boot and discovers the package again
- **THEN** the prior boot's runtime quarantine is cleared while persistent diagnostics may still report the previous failure

### Requirement: Report failure and return through host navigation
For a recovered plugin fault or timeout, EF800Application SHALL present a host-rendered LCD diagnostic identifying the affected plugin and failure class, require or apply host-owned acknowledgement policy, and return to the appropriate parent menu with no active plugin session.

#### Scenario: Measurement plugin fault is recovered
- **WHEN** the plugin supervisor completes safe recovery from a Measurement plugin fault
- **THEN** the LCD reports that the plugin failed and was disabled, and EF800Application returns to the parent Measurement menu after acknowledgement

#### Scenario: Calculation plugin times out
- **WHEN** the plugin supervisor safely terminates a timed-out Calculation plugin
- **THEN** the LCD reports the timeout and EF800Application returns to the Calculation menu without applying partial output

#### Scenario: Recovery is not safe
- **WHEN** fault classification or cleanup cannot prove that EF800Application and its resources remain in a known-safe state
- **THEN** EF800Application does not return to a menu and instead follows the controlled reboot policy

### Requirement: Reject packages incompatible with isolated execution
EF800Application SHALL validate the package architecture, isolation contract version, memory layout, entry points, section bounds, and required services before granting executable access. A package that requires direct privileged callbacks or cannot fit the target MPU layout SHALL be unavailable and SHALL NOT execute.

#### Scenario: Package supports isolated execution
- **WHEN** a package declares a compatible isolation contract and all code, data, entry points, and service requirements satisfy the host policy
- **THEN** EF800Application may activate it in the plugin worker domain

#### Scenario: Legacy package requires direct callbacks
- **WHEN** a package implements only a contract that exposes privileged host callback pointers
- **THEN** EF800Application rejects it as incompatible before executing any package entry point

#### Scenario: Package memory does not fit MPU constraints
- **WHEN** a package's aligned executable, read-only, writable, stack, and shared-memory requirements cannot be represented by the available MPU regions
- **THEN** EF800Application reports the package unavailable and does not weaken isolation to run it

### Requirement: Verify isolation and recovery on the production MCU
The release gate for external executable plugins SHALL include target tests on STM32L496 that inject representative memory, instruction, stack, timeout, and service-validation failures and demonstrate both plugin-only recovery and escalation for non-plugin faults. Native tests alone SHALL NOT qualify the MPU and fatal-error behavior.

#### Scenario: Target plugin fault injection passes
- **WHEN** the target test package performs each approved recoverable fault injection
- **THEN** EF800Application remains responsive, records the expected diagnostic, quarantines the package, restores host resources, and returns to the expected menu

#### Scenario: Target host fault injection passes
- **WHEN** a controlled test injects a fault outside the plugin worker boundary
- **THEN** the device follows the expected fatal/reboot path rather than claiming plugin recovery

#### Scenario: Isolation evidence is incomplete
- **WHEN** any required target fault, MPU access-control, timeout, cleanup, image-layout, or normal-plugin regression test has not passed
- **THEN** the EF800Application remains an engineering bring-up image, no production plugin release is claimed, and arbitrary packages are not treated as authentic or trusted; any package execution continues to use the isolated worker
