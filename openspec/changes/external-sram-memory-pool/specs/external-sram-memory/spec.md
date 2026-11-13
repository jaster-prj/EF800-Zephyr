# Spec Delta

## Purpose

Defines reliable use of the EF800 board's soldered FMC-connected SRAM by EF800Application, including hardware validation, application-owned allocations, and operation when external SRAM is unavailable.

## ADDED Requirements

### Requirement: Configure EF800 external SRAM from board and device specifications
The EF800 firmware SHALL configure its fitted external SRAM using the board schematic and the applicable STM32L496 and IS61 datasheet requirements. External SRAM support SHALL be a fixed board/build capability and SHALL NOT depend on a COMPOSITION selection.

#### Scenario: External SRAM is configured for the EF800 board
- **WHEN** an EF800 physical-target image initializes external SRAM
- **THEN** it uses the FMC bank, address mapping, bus wiring, and timing derived from the board schematic and device datasheets

### Requirement: Validate external SRAM before allocating runtime objects there
EF800Application SHALL validate external SRAM operation before placing application-owned or plugin-owned runtime allocations in it.

#### Scenario: SRAM validation succeeds
- **WHEN** startup validation verifies the configured external SRAM region
- **THEN** EF800Application may use the validated region according to its memory ownership and MPU policy

#### Scenario: SRAM validation fails
- **WHEN** external SRAM is absent, inaccessible, or fails validation
- **THEN** EF800Application does not allocate or execute from that region and continues using its internal-memory profile

### Requirement: Preserve EF800Application operation without external SRAM
EF800Application SHALL keep core application behavior available using internal SRAM when external SRAM cannot be validated, while reducing or disabling optional plugin support as required by the remaining memory budget.

#### Scenario: External SRAM is unavailable at startup
- **WHEN** EF800Application starts with external SRAM unavailable
- **THEN** core application functions remain available and plugins that exceed the internal-only budget are reported unavailable rather than preventing startup

### Requirement: Use validated external SRAM for suitable EF800Application allocations
When external SRAM is validated, EF800Application SHALL use it for designated eligible EF800Application-owned runtime allocations while retaining ownership and enforcing access constraints for those allocations.

#### Scenario: Application-owned allocation uses external SRAM
- **WHEN** EF800Application allocates an eligible object from the external region
- **THEN** the object remains owned and access-controlled by EF800Application and is not implicitly exposed to plugin workers

### Requirement: Validate external SRAM in the standalone hardware-debug image
The external SRAM hardware bring-up SHALL be independently testable in `test-hwdebug` before EF800Application depends on the region.

#### Scenario: Hardware-debug SRAM test passes
- **WHEN** the hwdebug test exercises the configured SRAM range
- **THEN** it reports success only after checking address boundaries, data patterns, and address aliasing without modifying unrelated memory

#### Scenario: Hardware-debug SRAM test detects a fault
- **WHEN** the test observes a read/write mismatch or alias
- **THEN** it reports the failing range and does not claim the memory region is usable

### Requirement: Preserve isolation properties for external-memory allocations
Use of external SRAM SHALL NOT weaken MPU access permissions, executable-memory separation, or plugin fault containment compared with the corresponding internal-memory profile.

#### Scenario: Plugin memory is placed in external SRAM
- **WHEN** a plugin is admitted to external SRAM-backed regions
- **THEN** its executable, read-only, writable, stack, and shared regions retain their required MPU permissions and unauthorized access remains fault-contained
