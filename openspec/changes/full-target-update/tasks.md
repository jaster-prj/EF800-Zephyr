## 1. Package Contract and Tooling

- [ ] 1.1 Define the versioned package envelope, target IDs, exact six FWU filenames, compatibility fields, integrity metadata, and parser bounds.
- [ ] 1.2 Add package generation and verification tooling for all six build artifacts, including target/load-address/size checks against the fixed image maps.
- [ ] 1.3 Add shared package parser tests for malformed headers, wrong targets, duplicates, unsupported versions, corrupt payloads, and boundary sizes.

## 2. Update Transaction and EF800 Images

- [ ] 2.1 Define and implement a persistent transaction journal with per-target phase, package identity/hash, result, restart recovery, and safe cleanup rules.
- [ ] 2.2 Extend the bootloader EF800Application installer to accept the EF800Application package contract while preserving current vector, bounds, integrity, readback, and cleanup guarantees.
- [ ] 2.3 Implement EF800Application-controlled BackupApplication package validation, programming, readback, and bootability verification within its fixed partition.
- [ ] 2.4 Prove the bootloader partition can be safely programmed while EF800Application runs from its separate partition; keep the feature disabled and report unsupported if flash execution, vector, interrupt, or watchdog safety is not established.
- [ ] 2.5 Implement the guarded EF800 Bootloader package update only after task 2.4 is satisfied, with explicit recovery status and ST-LINK recovery instructions.
- [ ] 2.6 Add native tests for EF800 image target mapping, partition bounds, restart at each transaction phase, interrupted writes, and verified-only package cleanup.

## 3. Keyboard Update Coordination

- [ ] 3.1 Extend EF800Application package selection from the single keyboard path to the distinct Updater, Bootloader, and Application package names.
- [ ] 3.2 Coordinate keyboard updates in Updater → Keyboard Bootloader → Keyboard Application order, checking service/version readiness after each target and stopping on failure.
- [ ] 3.3 Add tests for package routing, invalid target/package mismatch, missing packages, failed updater handoff, failure recovery, and exact completion reporting.

## 4. MTP Staging and Coordinator

- [ ] 4.1 Implement the deterministic full-set coordinator: EF800Application activation through the bootloader, BackupApplication, keyboard targets, then EF800 Bootloader; support explicitly reported skips and dependency failures.
- [ ] 4.2 Ensure MTP uploads are only staged files and serialize coordinator access against MTP, MSC, and filesystem ownership transitions.
- [ ] 4.3 Expose per-target update status and the required retry, recovery, or ST-LINK service action without exposing raw flash.
- [ ] 4.4 Test MTP completion/interruption, restart/resume behavior, simultaneous-storage-owner prevention, partial update sets, and end-to-end six-package progression.

## 5. Qualification and Documentation

- [ ] 5.1 Build all six firmware targets and run native package, boot/update, storage, and keyboard test suites.
- [ ] 5.2 Perform hardware qualification for every image independently, followed by staged-set, power-interruption, retry, BackupApplication recovery, and bootloader recovery tests.
- [ ] 5.3 Keep bootloader package updates release-gated until interruption/recovery tests pass; document Programmer fallback and update `docs/migration-status.md` and relevant update documentation with validated results.
