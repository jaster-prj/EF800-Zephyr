## Context

The work covers six EF800 images: MainApplication, the L496 second-level bootloader, BackupApplication, keyboard application, keyboard bootloader, and keyboard updater. MainApplication's `src/main.c` is approximately 7,700 lines and combines UI/navigation, workspace, measurement/ports/plugins, keyboard update, RTC/battery, and startup integration. The other images are smaller but still combine important concerns: the L496 bootloader coordinates validation, update, metadata, and handoff; the keyboard application combines matrix scanning, keyboard protocol, I2C services, and updater-partition programming; and the keyboard updater combines I2C framing, session policy, and flash operations. BackupApplication already separates MTP and composition services.

Existing code and tests contain behavioral boundaries worth preserving. Shared modules provide tested policy and wire-format logic; image CMake files compose those modules. MainApplication's common source manifest is shared by physical EF800 and native-sim builds. Native tests cover selected policies and protocols, while target-only behavior such as MPU enforcement, flash handoff, and electrical/peripheral operation remains a separate acceptance gate.

Existing OpenSpec capabilities remain the behavior source of truth, including flash layout and update bounds, keyboard update/recovery, BackupApplication MTP ownership, USB personality transitions, plugin isolation and host ownership, measurement plugin UI, and workspace editing/recovery. The refactor adds no intended externally visible behavior. In-force ADRs (no `Supersedes` fields were found) constrain the design: keep one physical/native-sim MainApplication composition (ADR-0006); retain package-owned measurement presentation and host-owned hardware/resources (ADR-0001, ADR-0010); preserve plugin isolation and recovery (ADR-0002); use native-sim only for behavioral evidence with explicit virtual adapters (ADR-0003, ADR-0005); preserve USB/QSPI ownership and restart boundaries (ADR-0011, ADR-0013); preserve journaled workspace replacement semantics (ADR-0014); and keep development-only legacy-QSPI formatting safeguards (ADR-0017).

## Goals / Non-Goals

**Goals:**
- Establish an auditable C style/quality profile, useful analyzer/compiler checks, and finding-disposition process for EF800-maintained C production and test code.
- Establish a domain/state/resource ownership map before moving code, then extract cohesive modules in reviewable increments.
- Preserve existing OpenSpec behavior, image contracts, hardware ownership, update protocols, and established test evidence.
- Add only material behavior tests identified by a specification-to-code-to-test gap review.
- Leave each firmware entry point as a comprehensible platform composition/startup boundary rather than a home for unrelated workflows.

**Non-Goals:**
- Changing menu behavior, protocols, image/partition addresses, plugin ABI, storage policy, or hardware behavior.
- Rewriting Zephyr, CMSIS, vendor, generated, or other third-party code to satisfy EF800's quality profile.
- Claiming that native-sim qualifies target MPU, boot handoff, flash physics, or electrical behavior.
- Imposing an arbitrary maximum function line count or mechanically splitting files without state-ownership analysis.
- Refactoring unrelated Python/web tooling or changing active external-SRAM feature requirements.

## Decisions

### 1. Establish compliance and behavior baselines before structural edits

Inventory C translation units by firmware/test target and ownership, including EF800-maintained shared modules, board integration, and EF800-authored plugin/test C sources built as part of those products. Determine generated and third-party inputs explicitly. Evaluate practical tooling (compiler diagnostics, Cppcheck, formatting/lint tools) for useful code-quality feedback, Zephyr/compiler configuration support, compile-command fidelity, and sustainable reporting. Prefer tools that can use real target build settings; clearly identify checks that cannot. This is a maintainability effort, not a formal MISRA compliance exercise.

Record a source-to-target manifest and a requirement traceability matrix: existing requirement/scenario, implementation entry points and domain state, current automated or hardware evidence, and any material gap. Establish a finding baseline. Fix actionable findings and give remaining findings explicit dispositions. Do not adopt broad or undocumented suppressions. Do not claim analyzer coverage for configurations or sources that were not actually checked.

**Alternative considered:** begin splitting `main.c` and select analysis tooling later. Rejected because it risks moving undocumented behavior and makes it harder to distinguish pre-existing findings from refactor regressions.

### 2. Extract by domain ownership, not by current file regions

Treat each image entry point as the composition root for its hardware, Zephyr services, initialization order, and domain dependencies. Move behavior behind cohesive domain APIs with module-owned mutable state. Use per-domain context structures and explicit operations/adapters when they clarify state ownership or make a meaningful behavior seam testable; avoid replacing current globals with a single oversized context object or adding abstraction with no ownership benefit. Keep hardware-facing adapters close to the image that owns the peripheral unless another image genuinely shares that adapter.

For MainApplication, first map and then separate major workflows (for example workspace browsing/editing, port assignment, measurement/plugin host flow, keyboard update, and presentation/rendering) from board/Zephyr integration. Preserve the shared production source manifest for physical and native-sim builds. Keep plugin-specific workflow in packages and privileged hardware/resource lifetime in the host as required by existing plugin specifications and ADRs.

For the L496 bootloader, distinguish boot decision, metadata/update validation, flash installation, watchdog policy, and architecture-specific handoff. For BackupApplication, retain startup ordering and exclusive MTP/filesystem ownership while keeping its existing MTP/composition modules. For the keyboard application, separate key-matrix and scan-code processing from I2C service framing and the updater-only flash transaction. For the keyboard bootloader, retain the bounded updater-entry/recovery decision separately from GPIO/I2C/flash/jump adapters. For the keyboard updater, separate callback-safe frame collection/response transport, update-session command policy, and partition-bounded flash operations.

Split functions around cohesive decision steps and state transitions. Use nesting, data/state flow, side effects, and testability as design signals; do not use a universal LOC cutoff as a substitute for review. CMake source manifests and include paths must make target ownership explicit and avoid pulling a target-specific implementation into another image accidentally.

**Alternative considered:** create one shared module for every similarly named application or move all image code into `shared/`. Rejected because similarly named images do not imply shared implementation; this would blur ownership and risks coupling images with different MCU, memory, and peripheral contracts.

**Alternative considered:** fully object-oriented service/ops abstractions for every function. Rejected because static firmware code benefits from explicit boundaries but does not need indirection where a direct, statically linked call has clear ownership.

### 3. Make behavior-preserving increments evidence-driven

For each extraction increment, name affected existing requirements, tests, build targets, and any target-only gate before editing. Prefer meaningful stateful/domain tests with observable invariants (for example update authorization and integrity, exact partition preservation, boot/recovery selection, USB/storage ownership, row-edit recovery, or plugin teardown/resource cleanup) over tests that assert a wrapper simply calls another function. Reuse existing native tests when they already provide adequate evidence. Add tests only for uncovered material behavior or a newly exposed module contract.

Run relevant native tests and firmware builds for each affected increment; retain full build, native suite, image-layout verification, and applicable target acceptance as closeout gates. Hardware gates remain explicit and are never inferred from a host or native-sim pass. Do not weaken protocol expectations, bounds, safe fallback, or ownership requirements to simplify extraction.

**Alternative considered:** defer testing until all files are split. Rejected because a large combined diff would make regressions and ownership mistakes difficult to localize.

### 4. Stage MainApplication work around the active SRAM change

The `external-sram-memory-pool` change still has MainApplication integration and regression tasks in progress. Complete or coordinate those changes before broad MainApplication source movement. Baseline analysis may proceed independently, but avoid concurrent restructuring of memory ownership, source lists, plugin admission, or allocation lifetimes. When refactoring resumes, preserve that change's internal-only fallback and plugin memory/MPU requirements.

### 5. Treat tool findings and residual gaps as first-class evidence

Keep a checked-in quality profile that identifies the selected C language mode, project style conventions, tool/version/configuration, analyzed target configurations, exclusion patterns, and analysis invocation. Keep a finding/suppression record with check, exact code scope, rationale, reviewer, and review status. Any suppression in source or tool configuration must be narrow and traceable to its rationale. Record residual tool limitations and any target build not analyzed; do not silently turn limitations into exclusions.

The initial tooling trial should include representative code from all six images, EF800 shared code, and test code, especially architecture-specific assembly/handoff and Zephyr callback/ISR integration, to verify compiler database import, external headers, target defines, and report usability. The initial MainApplication trial built successfully with a real compile database, but Cppcheck parsing failed on Zephyr compiler/configuration macros; guessed defines were not valid analysis. Evaluate alternate invocation/configuration or other practical checks. Do not force host defaults or claim a check passed when parsing was incomplete.

## Risks / Trade-offs

- [Selected tools cannot parse Zephyr target configuration or produce useful feedback] → Pilot against representative targets, use compiler warnings/format checks or another tool where suitable, and record unsupported coverage rather than presenting it as a pass.
- [Legacy code produces a large finding backlog] → Baseline findings by owned source/target, prioritize safety-critical paths, and gate changed/new code while scheduling existing debt in bounded increments; no undocumented bulk waiver.
- [Refactoring alters safety-critical order or resource cleanup] → Preserve existing behavior traces, use domain-level failure/cleanup tests, and maintain hardware-only validation gates.
- [New module boundaries duplicate shared policy or break image isolation] → Share only genuinely common contracts/policy, review the target source manifest, and build all consuming images.
- [Style checks obscure clear Zephyr integration] → Use narrowly scoped, reviewed suppressions for unavoidable framework/architecture constraints; do not contort safety-relevant behavior solely to silence a checker.
- [Concurrent SRAM work changes MainApplication allocations during extraction] → Coordinate sequencing and keep memory ownership changes distinct from source-boundary changes.

## Migration Plan

1. Inventory EF800-owned C sources and image/test compilation; map domain responsibilities, mutable state, hardware/resource ownership, existing specs, and behavioral evidence.
2. Evaluate practical C quality tools and run a representative pilot across each image family, shared code, and tests. Agree on the style profile, exclusions, baseline, reporting, and finding review process before broad remediation.
3. Add the maintainability/quality policy and traceability evidence without changing firmware runtime behavior.
4. Plan extractions as independent domain increments, prioritizing clear, testable ownership seams. Coordinate MainApplication code moves with the active external-SRAM change.
5. For every increment, preserve existing requirement scenarios; add only material tests missing from the trace; run affected target builds and relevant native tests.
6. Close out with all required firmware builds, the native regression suite, image contract verification, static analysis over the agreed production and test scope, and all applicable hardware gates. Report unresolved deviations and hardware evidence explicitly.

## Open Questions

- Which practical combination of compiler diagnostics, static analysis, and formatting checks produces useful target-aware feedback without creating an unsustainable false-positive burden?
- Who reviews new or changed suppressions, and what review cadence maintains the quality profile as EF800 code evolves?
- Which existing hardware gates can be run during each firmware-specific extraction, and which must remain deferred to the final target acceptance pass?
