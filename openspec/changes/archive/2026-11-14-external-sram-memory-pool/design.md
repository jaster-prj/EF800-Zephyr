# Design

## Context

See proposal.md for motivation. The EF800 board schematic identifies the fitted IS61WV5128FBLL-10TLI SRAM and FMC signal wiring. The EF800 DeviceTree currently exposes only internal `sram0`; `test-hwdebug` is a standalone peripheral-test image without FMC/SRAM setup. EF800 Application currently has fixed plugin arenas (32 KiB text, 32 KiB rodata, 4 KiB plugin RW within a 16 KiB mailbox region), a 2 KiB worker stack, one physical image slot, and a 32 KiB libc malloc arena. The worker MPU domain maps text RX, rodata RO, and mailbox/RW separately.

The memory controller configuration must be derived from the STM32L496 and IS61 datasheets and schematic. The existing board clock is configured for 80 MHz. A build map and target measurements are needed before selecting application allocations to move or final arena sizes.

## Goals / Non-Goals

**Goals:**
- Prove FMC initialization and the SRAM address range in `test-hwdebug` before EF800 Application depends on it.
- Make external SRAM a fixed EF800 board/build resource, with runtime validation and a safe internal-memory fallback.
- Use external memory for eligible EF800 Application allocations and plugin-owned memory, while preserving EF800 Application ownership and worker MPU isolation.
- Replace fixed plugin capacities with validated per-package owned-memory requirements within available memory and MPU constraints.

**Non-Goals:**
- Selecting SRAM availability or policy through COMPOSITION.
- Moving hardware drivers, peripheral state, or host service stacks into plugin ownership.
- Requiring external SRAM for boot-critical state or core EF800 Application operation.
- Assuming all Zephyr stacks, DMA buffers, or arbitrary static sections can reside in external SRAM without separate validation.

## Decisions

### Stage hardware bring-up in `test-hwdebug`

Use the standalone hwdebug image to initialize FMC from the datasheets and board wiring, then test the complete 512 KiB range with address-sensitive patterns, boundaries, and alias checks. Test code must avoid corrupting the running image, stack, or other memory. Only after basic integrity passes, add Zephyr-visible region and MPU tests. EF800 Application integration is gated on successful STM32L496 target evidence.

**Alternative considered:** add FMC setup directly to the normal board and debug EF800 Application failures there. Rejected because it couples new hardware bring-up to an already memory-constrained application and makes failures harder to isolate.

### Treat external SRAM as a fixed board capability with runtime validation

The EF800 physical build includes FMC/SRAM support unconditionally; COMPOSITION does not select it. Startup validates the region before allocation. A failed validation marks the external pool unavailable for that boot and selects the internal-only memory profile.

**Alternative considered:** assume the SRAM is always usable because it is soldered. Rejected because wiring, component, controller, or timing faults must not make core application startup depend on unverified memory.

### Separate external and internal allocation profiles

Keep boot-critical code, initialization state, and the minimum core application footprint in internal SRAM. After external SRAM validation, expose it as an explicit application-owned pool for eligible large allocations. Keep an internal fallback pool sized for core behavior and a deliberately reduced plugin budget. Avoid linker placement of fallback-critical state exclusively in external RAM.

Plugin and application allocators must use distinct ownership/accounting. Plugin allocations are granted only after package validation and are mapped into the unprivileged worker with their required MPU permissions. Host service allocations (including future Bluetooth stack memory) remain EF800 Application-owned and outside the plugin package declaration.

**Alternative considered:** place all large static objects in external SRAM at link time. Rejected as the initial strategy because an uninitialized external region would make those objects unusable before runtime fallback could be selected.

### Make plugin capacity package-declared and host-bounded

Extend the package metadata/loader contract with bounded plugin-owned text, rodata, writable data/BSS, stack, and shared-buffer requirements. Validate declarations against actual loaded sections, target region limits, alignment, current pool availability, and MPU representability before execution. Do not treat declarations as authority to allocate arbitrary memory. Keep one active physical plugin image unless a separately justified concurrency design changes that limit.

**Alternative considered:** increase the global fixed arenas to the SRAM chip's full capacity. Rejected because it reserves scarce resources regardless of installed plugin needs and does not account for EF800 Application allocations or MPU layout limits.

### Choose allocations from measured memory evidence

Capture a pristine EF800 Application map and runtime high-water measurements before moving objects. Prioritize plugin image arenas and bounded package/loading or calculation scratch buffers where lifetimes are clear. Move additional EF800 Application-owned buffers only when external placement reduces internal pressure and their access/performance constraints are understood. Do not move stacks or DMA buffers by default.

## Risks / Trade-offs

- [FMC timing or pin configuration is incorrect] → Derive settings from both datasheets and schematic; validate progressively in hwdebug with address/data patterns and sustained target tests.
- [External SRAM is readable but MPU or instruction-fetch behavior differs] → Gate plugin use on L496 MPU permission and execute tests; reject placements that cannot preserve RX/RO/RW-XN policy.
- [Fallback still reserves too much internal RAM] → Use map evidence to size the internal fallback deliberately and report plugin unavailability for packages that do not fit.
- [External accesses affect latency or timing] → Measure worst-case plugin and application paths before moving performance-sensitive allocations.
- [Plugin declarations do not match actual use] → Cross-check declared section sizes and stack/shared limits against parsed package layout; enforce bounds at allocation and MPU setup.
- [Host service memory is mistakenly charged to a plugin] → Keep service ownership and budgets in EF800 Application; plugin declarations cover only plugin-owned memory.

## Migration Plan

1. Add and validate external SRAM support only in `test-hwdebug`; retain the current EF800 Application image unchanged during this gate.
2. Add Zephyr region, allocator, and MPU tests on the target; stop integration if they cannot meet isolation requirements.
3. Integrate fixed board support and startup validation into EF800 Application, preserving an internal-only startup path.
4. Use link-map and runtime measurements to migrate selected EF800 Application buffers and establish separate internal/external budgets.
5. Introduce declared plugin-owned memory requirements and validate package admission and worker isolation under both memory profiles.
6. Run full target plugin fault/recovery and application regression tests. On regression, disable external-pool use and retain the internal-only profile; do not weaken MPU policy.

## Open Questions

- The exact FMC bank/address region and timing register values must be calculated from the schematic, MCU clock configuration, and both device datasheets during implementation; no values are assumed by this design.
- Final EF800 Application allocation placement and fallback/plugin capacity require a current link map and target high-water measurements.
- The external SRAM's use for user thread stacks or DMA buffers remains excluded until the relevant Zephyr/MPU/DMA behavior is demonstrated.
