# ef800-firmware-maintainability Specification

## Purpose

TBD: Update Purpose after archive.

## Requirements

### Requirement: EF800-owned C code follows a documented quality profile
EF800-owned production and test C code across the six firmware images, shared modules, and EF800 board integration SHALL follow a documented project C style and quality profile based on repository conventions, compiler diagnostics, and practical automated checks. The selected checks SHALL use actual Zephyr compiler and target build options where supported. Zephyr/vendor and generated sources SHALL be identified separately from EF800-owned results. No particular MISRA edition or formal compliance claim is required.

#### Scenario: Production and test sources are assessed
- **WHEN** the compliance analysis runs for a supported firmware or test target
- **THEN** it analyzes the EF800-owned C sources compiled for that target with matching build configuration and identifies excluded third-party or generated inputs explicitly

#### Scenario: New EF800-owned C code is added
- **WHEN** production or test C code is added to a supported build
- **THEN** the code is included in the applicable quality checks using that build's target configuration where supported

### Requirement: Code-quality findings have explicit reviewed dispositions
Actionable findings from the selected compiler and quality tools in EF800-owned code SHALL be fixed or explicitly dispositioned. Existing findings may be baselined with scope and rationale; new findings SHALL NOT be silently added. Any suppression SHALL be narrow, documented with its rationale, and reviewable; broad or undocumented suppressions SHALL NOT be used.

#### Scenario: A checker finding is reported
- **WHEN** the checker reports an applicable finding in EF800-owned code
- **THEN** the finding is fixed or receives a documented disposition, and no new finding is silently suppressed

#### Scenario: A deviation is proposed or changed
- **WHEN** a tool suppression or quality-rule exception is added or modified
- **THEN** its check, exact scope, rationale, and review approval are recorded

### Requirement: Firmware modules have explicit domain and state ownership
The six EF800 firmware images and their EF800-owned shared code SHALL organize behavior into cohesive domain modules with explicit ownership of mutable state and hardware resources. Image entry points SHALL compose and initialize domains rather than retain unrelated workflows. Related state SHALL be grouped into scoped structures when this clarifies its ownership.

#### Scenario: A firmware domain is extracted
- **WHEN** a domain is moved from an image entry point into a module
- **THEN** the module boundary identifies its owned state, inputs, outputs, and resource interactions without changing the externally observable firmware behavior

#### Scenario: An entry point initializes firmware domains
- **WHEN** a supported image starts
- **THEN** its entry point retains startup ordering and platform integration while delegating domain behavior through explicit interfaces

### Requirement: Complex functions are decomposed by behavior
Large or complex functions SHALL be decomposed at meaningful responsibility, decision, or state-transition boundaries. An arbitrary source-line limit SHALL NOT substitute for behavioral cohesion.

#### Scenario: A complex function is decomposed
- **WHEN** a function combines distinct responsibilities or multiple state transitions
- **THEN** the implementation separates cohesive behavior and preserves the decision order, error handling, and resource cleanup of the original flow

### Requirement: Refactoring preserves specified firmware behavior
The refactoring SHALL preserve existing OpenSpec requirements, image and partition layouts, wire protocols, storage and USB ownership, plugin isolation boundaries, recovery behavior, and user-visible workflows.

#### Scenario: Existing requirement is affected by a source move
- **WHEN** an implementation path covered by an existing specification is moved or reshaped
- **THEN** its applicable requirement scenarios remain valid and their existing behavioral evidence continues to pass

### Requirement: Tests protect material behavior gaps
Existing requirement scenarios SHALL be mapped to automated or hardware acceptance evidence. New tests SHALL address material behavior gaps and exercise meaningful domain or integration behavior, not merely trivial wrappers or relocated functions.

#### Scenario: A material behavior gap is found
- **WHEN** the trace from a specified scenario to its implementation and test evidence identifies an unprotected behavior or important boundary
- **THEN** a test is added at the narrowest useful domain or integration seam to verify the behavior, including relevant failure and cleanup outcomes where specified

#### Scenario: A full refactoring increment is validated
- **WHEN** a staged refactoring increment is proposed as complete
- **THEN** affected firmware targets build, relevant native tests pass, the fixed image contract is checked when applicable, and hardware-only acceptance evidence remains explicitly required rather than inferred from native tests
