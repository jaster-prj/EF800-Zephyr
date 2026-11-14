# Tasks

## 1. FMC SRAM Bring-Up in Hardware Debug

- [x] 1.1 Derive FMC bank, address mapping, bus width, pin assignment, and timing from `schema/EF800_NEW.kicad_sch`, the STM32L496 documentation, the IS61 datasheet, and the board clock; record the derivation in hwdebug documentation and verify it against the schematic and datasheet timing limits.
- [x] 1.2 Add FMC initialization and a bounded SRAM test to `test-hwdebug`; verify the full 512 KiB range with address/data patterns, boundaries, alias checks, and repeatable target runs, reporting failures without claiming the region usable.
- [x] 1.3 Build the hwdebug image in the prescribed devcontainer and verify the new generated DeviceTree/link map and SRAM test command on STM32L496 hardware; document the test procedure and results in the hwdebug documentation.

## 2. Zephyr Memory Region and Protection Proof

- [x] 2.1 Describe the validated external range as a distinct EF800 memory region without making it ordinary internal SRAM; verify generated DeviceTree and linker regions do not overlap internal SRAM or fixed flash images.
- [x] 2.2 Add target tests for Zephyr allocation/access and MPU RX, RO, RW/XN, and denied-access behavior using external SRAM; verify positive accesses and expected faults on STM32L496, and document unsupported stack/DMA use unless separately demonstrated.
- [x] 2.3 Verify external-memory test coverage and all affected firmware builds; stop EF800 Application integration if MPU permissions, execution, or memory boundaries cannot be proven.

## 3. EF800 Application Memory Pools and Fallback

- [x] 3.1 Capture the pristine EF800 Application link map and runtime high-water baseline; verify reported internal SRAM totals and document candidate application-owned buffers with measured sizes and lifetimes.
- [x] 3.2 Add startup validation and separate external/internal allocation pools, keeping boot-critical state internal; verify failed SRAM validation selects the internal-only profile and EF800 Application core services still start.
- [x] 3.3 Move only measured, eligible EF800 Application allocations to the external pool and expose internal/external budget diagnostics; verify memory accounting, application behavior, and fallback using target tests and updated memory documentation.

## 4. Plugin-Owned Memory Requirements

- [x] 4.1 Extend package metadata/SDK inputs to declare bounded plugin-owned text, rodata, data/BSS, stack, and shared-buffer requirements; verify generated packages include consistent bounds and update the plugin contract documentation.
- [ ] 4.2 Validate declarations against parsed sections, actual allocation sizes, active memory availability, alignment, and MPU layout before loading/execution; verify native tests reject missing, malformed, oversized, and inconsistent declarations without weakening isolation.
- [ ] 4.3 Integrate external and internal-only plugin pools while keeping host service allocations separate; verify eligible packages run, over-budget packages are diagnosed unavailable, and plugin fault/recovery behavior remains covered by native tests and target tests.

## 5. Target Integration and Regression Gate

- [ ] 5.1 Run the STM32L496 acceptance matrix with external SRAM enabled, including plugin RX/RO/RW-XN permissions, memory faults, timeouts, recovery, and EF800 Application-owned allocation behavior; verify expected diagnostics and resource cleanup.
- [x] 5.2 Exercise external-SRAM startup failure and internal-only fallback on target; verify core EF800 Application functions remain usable and only plugins exceeding the internal budget are unavailable.
- [ ] 5.3 Run the four firmware builds, native test suite, image-layout verification, and relevant target acceptance tests; update `docs/migration-status.md` and memory/security documentation with evidence and remaining release gates.
