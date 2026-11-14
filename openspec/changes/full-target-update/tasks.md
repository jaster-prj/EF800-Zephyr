## 1. Package Contract and Tooling

- [x] 1.1 Define the versioned package envelope, target IDs, exact six FWU filenames, image semantic version and minimum coordinator-version fields, integrity metadata, and parser bounds.
- [x] 1.2 Add package generation and verification tooling for all six build artifacts, including target/load-address/size checks against the fixed image maps, mandatory per-image version increments, and the one-time legacy `EF800_FW.bin` bootstrap artifact for EF800 Application 1.0.0.
- [x] 1.3 Add shared package parser tests for malformed headers, wrong targets, duplicates, unsupported versions, corrupt payloads, and boundary sizes.

## 2. Update Transaction and EF800 Images

- [x] 2.1 Define and implement a persistent transaction journal with per-target phase, package identity/hash, result, restart recovery, and safe cleanup rules.
- [x] 2.2 Extend the bootloader EF800 Application installer to accept the EF800 Application package contract for subsequent updates while preserving current vector, bounds, integrity, readback, and cleanup guarantees; retain the legacy `EF800_FW.bin` path only for the one-time bootstrap.
- [x] 2.3 Implement EF800 Application-controlled EF800 Backup package validation, programming, readback, and bootability verification within its fixed partition.
- [ ] 2.4 Establish and verify the EF800 Application-side programming safety conditions: execution and live vectors remain outside the Bootloader partition; flash operations are bounded and watchdog-serviced; interrupted writes remain diagnosable and identify possible ST-LINK recovery.
- [x] 2.5 Implement the build-gated, explicitly invoked `bootloader_update` shell command for staged `EF800_BOOT.pkg`; validate the package and fixed partition, journal each phase, verify readback/vector state, and clean the package only after success. Startup coordination must never trigger Bootloader programming.
- [x] 2.6 Add native tests for EF800 image target mapping, partition bounds, restart at each transaction phase, interrupted writes, and verified-only package cleanup.

## 3. Keyboard Update Coordination

- [x] 3.1 Extend EF800 Application package selection from the single keyboard path to the distinct Updater, Bootloader, and Application package names.
- [x] 3.2 Coordinate keyboard updates in Updater → Keyboard Bootloader → Keyboard Application order, checking service/version readiness after each target and stopping on failure.
- [x] 3.3 Add tests for package routing, invalid target/package mismatch, missing packages, failed updater handoff, failure recovery, and exact completion reporting.

## 4. MTP Staging and Coordinator

- [x] 4.1 Implement the deterministic post-bootstrap coordinator: EF800 Application updates use `EF800_APP.pkg`, followed by EF800 Backup and keyboard targets; leave EF800 Bootloader staged for an explicit shell request. Support explicitly reported skips and dependency failures.
- [x] 4.2 Ensure MTP uploads are only staged files and serialize coordinator access against MTP, MSC, and filesystem ownership transitions.
- [x] 4.3 Expose per-target update status and the required retry, recovery, or ST-LINK service action without exposing raw flash.
- [x] 4.4 Test MTP completion/interruption, restart/resume behavior, simultaneous-storage-owner prevention, partial update sets, and end-to-end six-package progression.

## 5. Qualification and Documentation

- [ ] 5.1 Build all six firmware targets and run native package, boot/update, storage, and keyboard test suites.
- [ ] 5.2 Perform hardware qualification for every image independently, followed by staged-set, power-interruption, retry, EF800 Backup recovery, and EF800 Bootloader recovery tests.
- [ ] 5.3 Keep bootloader package updates release-gated until interruption/recovery tests pass; document Programmer fallback and update `docs/migration-status.md` and relevant update documentation with validated results.
