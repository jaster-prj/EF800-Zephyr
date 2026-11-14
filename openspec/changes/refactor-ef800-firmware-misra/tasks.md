## 1. Establish Source, Behavior, and Tooling Baselines

- [x] 1.1 Inventory EF800-owned C production and test translation units by firmware/test target, including shared, board, and packaged plugin code; explicitly classify third-party and generated sources.
- [x] 1.2 Trace existing OpenSpec requirements and scenarios to source domains, current native/automated tests, and hardware acceptance evidence for all six images; record only material evidence gaps.
- [x] 1.3 Evaluate practical C code-quality tooling (compiler diagnostics, Cppcheck checks, formatting/lint tools as appropriate) against actual Zephyr compiler modes, target flags, compile databases, test configurations, language support, reports, and suppression workflow; run a representative pilot across all six image families and tests.
- [x] 1.4 Select and document a useful tool/style profile, scoped exclusions, baseline, repeatable analysis invocation, and review/maintenance process for findings and suppressions; report unsupported configurations rather than claiming coverage.
- [x] 1.5 Record current firmware ownership and dependency maps, function/state hotspots, image source manifests, and resource lifetimes before moving implementations.

## 2. Refactor Main Bootloader and EF800 Backup

- [x] 2.1 Separate L496 boot decision orchestration, update validation/install work, metadata/watchdog coordination, and architecture-specific handoff at explicit interfaces without changing startup order or recovery policy.
- [x] 2.2 Preserve EF800 Application-only automatic update authorization and prove invalid/truncated/CRC-failing packages cannot erase or alter protected image partitions using existing or materially expanded behavior tests.
- [x] 2.3 Review EF800 Backup startup, QSPI mounts, watchdog, composition, and MTP adapters; extract only a cohesive missing domain boundary and preserve exclusive filesystem/USB ownership and cleanup.
- [x] 2.4 Build both images and run relevant boot/update, metadata, and Backup MTP tests; verify flash/image layout assumptions remain unchanged.

## 3. Refactor Keyboard Firmware Family

- [x] 3.1 Map and separate keyboard matrix scanning/debounce, scan-code and UART protocol handling, application I2C service behavior, and updater-partition update transactions with clear owned state.
- [x] 3.2 Preserve the keyboard application's updater-only authorization and verified handoff; preserve normal key reporting, I2C status/version behavior, and command serialization.
- [x] 3.3 Separate keyboard bootloader recovery/updater-entry policy from its GPIO/I2C/flash and image-handoff adapters while retaining bounded probe timing and watchdog behavior.
- [x] 3.4 Separate updater I2C frame transport, update-session command state, and bounded partition Flash operations; preserve target authorization, checksums, integrity checks, retry/recovery, and response-before-reboot behavior.
- [x] 3.5 Use the existing protocol/update tests as behavior evidence and add tests only for uncovered meaningful command/session/failure invariants; build keyboard application, bootloader, and updater and verify their partition/image limits.

## 4. Refactor EF800 Application by Domain

- [x] 4.1 Coordinate EF800 Application source movement with completion or explicit sequencing of `external-sram-memory-pool`; preserve its memory ownership, fallback, plugin admission, and MPU requirements.
- [x] 4.2 Extract workspace browser/editor and position workflows with bounded state and explicit persistence operations; preserve ordering, row-ending, conflict detection, and crash-recovery behavior from the workspace specifications.
- [x] 4.3 Extract port assignment and measurement/plugin host workflows with explicit state/resource ownership; preserve plugin-owned measurement UI, host-owned hardware/navigation, plugin isolation, UART claims, and assignment reconciliation.
- [x] 4.4 Separate menu/presentation rendering from navigation/workflow decisions while keeping the host-owned display header, global keys, T6963 boundary, and bounded plugin display model intact.
- [x] 4.5 Separate keyboard update/package handling and remaining RTC, battery, shell, startup, and virtual-platform orchestration into cohesive domains where the source/state map supports a clear boundary.
- [x] 4.6 Keep physical and native-sim builds on the same EF800 Application production source manifest; update source/include manifests without mixing target-specific adapters across images.
- [x] 4.7 Run affected native behavior tests and both EF800 Application builds after each domain increment; add only tests justified by the specification-to-evidence gap map.

## 5. Apply and Gate C Code-Quality Checks

- [x] 5.1 Run the selected target-aware compiler/style/static-analysis checks on EF800-owned production and test C sources; confirm generated/third-party exclusions and tool limitations are explicit and reproducible.
- [x] 5.2 Fix actionable findings and record reviewed dispositions for existing findings; remove unjustified, broad, or undocumented suppressions.
- [x] 5.3 Add a repeatable quality check for new/changed EF800-owned C code and maintain a finding/suppression record; use real target configurations where supported and report unsupported coverage rather than masking it.
- [ ] 5.4 Add automatic CI gates for Cppcheck findings and CScout CC metrics on explicit EF800-owned translation units; preserve target build configuration, exclude third-party findings, establish a reviewed complexity baseline, and fix actionable findings.

## 6. Regression and Acceptance Closeout

- [x] 6.1 Build EF800 Application, L496 bootloader, EF800 Backup, keyboard application, keyboard bootloader, and keyboard updater in the prescribed devcontainer.
- [x] 6.2 Run the complete native test suite and all affected behavior tests; verify the specification-to-test trace has no unaddressed material regressions or unjustified test gaps.
- [x] 6.3 Run image-size/address verification and keyboard partition/package checks; confirm no image layout, update boundary, protocol, or ownership contract changed.
- [x] 6.4 Complete applicable target acceptance for boot handoff/update, keyboard protocol/update recovery, plugin MPU/fault behavior, and physical storage/USB/peripheral paths; report any unavailable hardware gate explicitly rather than inferring a pass.
- [x] 6.5 Update migration/status documentation with module ownership, quality tools/profile, remaining findings and suppressions, test/build evidence, and uncompleted hardware gates.
