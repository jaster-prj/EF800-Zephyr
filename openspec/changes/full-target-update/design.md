## Context

The current bootloader installer accepts a raw or optionally-headered EF800Application image at one fixed path, validates its vector and optional CRC, programs only the EF800Application partition, and removes the staged file after successful verification. EF800Application has an I2C package workflow for three keyboard targets, but currently reads one fixed `EF800_KEYBOARD.pkg` path. The current flash-layout spec and ADR-0013 explicitly reserve bootloader replacement for ST-LINK and exclude BackupApplication replacement from automatic updates. MTP provides filesystem-mediated FWU storage; it does not expose raw internal flash.

The proposed update set contains six target-specific packages: `EF800_BOOT.pkg`, `EF800_BACKUP.pkg`, `EF800_APP.pkg`, `EF800_KEYBOARD_UPDATER.pkg`, `EF800_KEYBOARD_BOOT.pkg`, and `EF800_KEYBOARD_APP.pkg`. The fixed L496 and L431 flash maps remain compatibility contracts. Keyboard updater handoff and physical update interruption behavior are not fully hardware-qualified.

## Goals / Non-Goals

**Goals:**
- Stage all six independently identifiable packages in FWU and process them deterministically with per-target status.
- Reuse the existing bootloader EF800Application update path and keyboard update protocol where possible, extending package selection and coordination.
- Validate target, format, version, bounds, vector where applicable, and integrity before writing; verify the programmed image before marking a stage complete.
- Preserve the working recovery image while updates proceed and make failure/recovery actions explicit.

**Non-Goals:**
- Host-side raw flash access or MTP exposure of internal flash.
- Changing any image address, partition capacity, keyboard protocol wire contract, or QSPI geometry.
- Claiming physical qualification before interruption, recovery, and all six update paths pass hardware testing.
- Automatically downgrading images or accepting packages for a different hardware revision.

## Decisions

1. **Use six target-bound package names and a shared versioned envelope.** Each envelope declares target, format version, image metadata, and integrity. EF800 and keyboard payload validators retain target-specific rules. A package with a target/name mismatch is rejected before erase. Alternatives: keep raw `.bin` files and infer target from filenames (rejected because filenames alone are not trustworthy), or combine all six payloads into one opaque bundle (rejected because it complicates per-image retry and completion).

2. **Use a coordinator state machine, not directory-order discovery.** The coordinator explicitly maps names to target kinds, validates the set, records each phase durably, and owns resume/stop decisions. Processing is: bootloader installs `EF800_APP.pkg` at startup; EF800Application validates and updates BackupApplication; then Keyboard Updater, Keyboard Bootloader, and Keyboard Application; EF800 Bootloader is last. This keeps the existing known EF800Application activation boundary and leaves the bootloader as the last EF800 image changed. Alternatives: lexical filename order (rejected as accidental and unsafe), update the bootloader first (rejected because it is a recovery anchor), or attempt all targets in one uninterrupted run (rejected because updates require reboots and different MCU protocols).

3. **Install EF800 Bootloader only from EF800Application-controlled code while EF800Application executes from its separate partition.** EF800Application must not delegate its own bootloader package to the currently executing bootloader. The implementation must establish that flash erase/program operations can run safely from EF800Application with interrupts, vectors, constants, and flash-driver routines accounted for; if this cannot be proven, retain ST-LINK-only installation and report the bootloader package as unsupported rather than risk an unbootable device. A redundant bootloader or SRAM-resident installer is an alternative requiring separate layout/resource analysis.

4. **Treat update progress and package cleanup as a transaction.** Persist target, phase, package identity/hash, and result in bounded boot-state metadata or a dedicated journal. Do not delete a package until installed-image readback and bootability checks pass. A restart resumes only a verified or explicitly retryable stage; an ambiguous interrupted erase requires a defined recovery report. Keyboard package transfer stays serialized with the existing I2C protocol and existing package CRC/vector checks.

5. **Keep MTP as staging only and enforce storage ownership.** Packages are uploaded through existing FWU paths, and the coordinator consumes them only when the relevant firmware exclusively owns shared FAT. BackupApplication recovery MTP and EF800Application must never mount the shared volume simultaneously. MTP itself does not trigger programming during upload.

## Risks / Trade-offs

- [Bootloader replacement can make the unit unbootable if power fails] → defer it until all other images pass; require a proven separate-partition programming path and explicit ST-LINK recovery instructions; do not enable it until hardware interruption tests pass.
- [The 128 KiB BackupApplication may not have room for package scanning or update code] → keep the coordinator in EF800Application and keep BackupApplication MTP limited to staging.
- [Progress metadata can become inconsistent with a partially programmed image] → journal phase transitions, re-read/validate target flash on restart, and never trust a completion bit alone.
- [Keyboard updater refresh can disrupt subsequent keyboard updates] → verify updater version and service availability after refresh before continuing; stop safely on mismatch.
- [MTP and update execution can contend for FAT or MSC ownership] → serialize coordinator startup against MTP/MSC and require filesystem unmount/sync before restart or programming.
- [Current physical keyboard updater entry has documented failures] → treat keyboard update ordering as intended behavior, not qualified capability, until the hardware test matrix passes.

## Migration Plan

1. Add package producer/verification tooling and test malformed, wrong-target, duplicate, and interrupted package sets.
2. Implement transaction journaling, package validation, status reporting, and EF800Application/BackupApplication update paths while keeping bootloader update disabled.
3. Implement and validate the bootloader update mechanism separately; only enable it after targeted power-loss and recovery tests.
4. Integrate keyboard packages and test each keyboard target independently before enabling full-set progression.
5. Stage a complete six-package set through MTP, validate every completion/status transition, and run the recovery matrix. Preserve the current Programmer procedure as fallback.

Rollback is per image, not atomic across all six. If an update fails, the coordinator stops at the failed stage, preserves uncompleted packages, and reports whether normal boot, retry, BackupApplication recovery, or ST-LINK service is required.

## Open Questions

- Can the STM32L496 safely program the bootloader partition from EF800Application with all flash-resident execution, vectors, interrupt handlers, and watchdog behavior accounted for, or is a RAM-resident installer required?
- Which persistent area can safely hold the transaction journal without changing the fixed flash/QSPI compatibility contracts?
- Should the package envelope use a cryptographic signature in addition to CRC, and what key provisioning/anti-rollback policy applies? Until decided, CRC provides corruption detection only, not authenticity.
- What minimum/maximum version compatibility rules apply between bootloader, EF800Application, BackupApplication, and keyboard images?
- Does absent package mean skip by default, or should a user explicitly authorize a partial update set?
