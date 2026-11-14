## Context

The currently deployed bootloader installer accepts a raw or optionally-headered EF800 Application image only at `/DATA:/fwu/EF800_FW.bin`, validates its vector and optional CRC, programs only the EF800 Application partition, and removes the staged file after successful verification. It cannot install the new `EF800_APP.pkg` envelope. Therefore the first versioned EF800 Application image is a one-time bootstrap delivered as `EF800_FW.bin`; only after that image is running can the new coordinator process the six-package format. EF800 Application has an I2C package workflow for three keyboard targets and now routes them by `EF800_KEYBOARD_UPDATER.pkg`, `EF800_KEYBOARD_BOOT.pkg`, and `EF800_KEYBOARD_APP.pkg`. The current flash-layout spec and ADR-0013 explicitly reserve bootloader replacement for ST-LINK and exclude EF800 Backup replacement from automatic updates. MTP provides filesystem-mediated FWU storage; it does not expose raw internal flash.

The normal coordinated update set contains six target-specific packages: `EF800_BOOT.pkg`, `EF800_BACKUP.pkg`, `EF800_APP.pkg`, `EF800_KEYBOARD_UPDATER.pkg`, `EF800_KEYBOARD_BOOT.pkg`, and `EF800_KEYBOARD_APP.pkg`. The one-time bootstrap is the exception: it uses the existing `EF800_FW.bin` path to install EF800 Application 1.0.0, then the coordinator installs the remaining images as version 1.0.0 packages. Every later change to an image must increment that image's version. Each package declares its image version and minimum compatible coordinator version; all coordinated packages in this migration require coordinator version 1.0.0, and no maximum version is imposed unless a later compatibility decision requires one. The fixed L496 and L431 flash maps remain compatibility contracts. Keyboard updater handoff and physical update interruption behavior are not fully hardware-qualified.

## Goals / Non-Goals

**Goals:**
- Stage all six independently identifiable packages in FWU and process them deterministically with per-target status.
- Reuse the existing bootloader EF800 Application update path and keyboard update protocol where possible, extending package selection and coordination.
- Establish a one-time legacy-path bootstrap for EF800 Application 1.0.0 before the package coordinator is available on the device.
- Version all six images as 1.0.0 in this migration; require each subsequent change to an image to increment that image's version.
- Carry a minimum compatible coordinator version in each package; the initial coordinated package set requires version 1.0.0.
- Validate target, format, version, bounds, vector where applicable, and integrity before writing; verify the programmed image before marking a stage complete.
- Preserve the working recovery image while updates proceed and make failure/recovery actions explicit.

**Non-Goals:**
- Host-side raw flash access or MTP exposure of internal flash.
- Changing any image address, partition capacity, keyboard protocol wire contract, or QSPI geometry.
- Claiming physical qualification before interruption, recovery, and all six update paths pass hardware testing.
- Automatically downgrading images or accepting packages for a different hardware revision.

## Decisions

1. **Use six target-bound package names and a shared versioned envelope.** Each envelope declares target, format version, image metadata, and integrity. EF800 and keyboard payload validators retain target-specific rules. A package with a target/name mismatch is rejected before erase. Alternatives: keep raw `.bin` files and infer target from filenames (rejected because filenames alone are not trustworthy), or combine all six payloads into one opaque bundle (rejected because it complicates per-image retry and completion).

2. **Use a coordinator state machine, not directory-order discovery.** The coordinator explicitly maps names to target kinds, validates the set, records each phase durably, and owns resume/stop decisions. Migration has two phases: first, the currently deployed bootloader installs EF800 Application 1.0.0 from the legacy `EF800_FW.bin` path; then that application coordinates the version-1.0.0 packages for EF800 Backup, Keyboard Updater, Keyboard Bootloader, Keyboard Application, and finally EF800 Bootloader. In subsequent updates, EF800 Application updates use `EF800_APP.pkg` and all six targets use their package names. This preserves the currently supported activation boundary during bootstrap and leaves the bootloader as the last image changed. Alternatives: require a separate ST-LINK bootstrap (rejected because it adds a manual operation to the normal migration), lexical filename order (rejected as accidental and unsafe), update the bootloader first (rejected because it is a recovery anchor), or attempt all targets in one uninterrupted run (rejected because updates require reboots and different MCU protocols).

3. **Install EF800 Bootloader only by explicit EF800 Application shell request.** The package is `EF800_BOOT.pkg` in `/DATA:/fwu`; package presence at boot is not authorization to mutate the recovery image. A build-time Kconfig gate must expose the manual shell command, and runtime checks must require exclusive filesystem ownership. EF800 Application must not delegate its own bootloader package to the currently executing bootloader. Before erase, validate the complete package, vector, fixed partition geometry, and that the live Application/vector context is outside the target. Erase/program only the Bootloader partition, feed the watchdog between bounded flash operations, read back and verify the full image, and clean up only after verified completion. Failures retain the package and report ST-LINK recovery as potentially required.

4. **Treat update progress and package cleanup as a transaction.** Persist target, phase, package identity/hash, and result in bounded boot-state metadata or a dedicated journal. Do not delete a package until installed-image readback and bootability checks pass. A restart resumes only a verified or explicitly retryable stage; an ambiguous interrupted erase requires a defined recovery report. Keyboard package transfer stays serialized with the existing I2C protocol and existing package CRC/vector checks.

5. **Store the transaction journal in the existing boot-state NVS.** Use a separate NVS record ID in the existing first-32-KiB QSPI boot-state partition, independently of the boot-selection metadata record. Keep the journal bounded, versioned, CRC-protected, and written/read back through the same NVS adapter. Do not allocate internal flash or alter the QSPI partition map. The NVS journal is accessed only by the active firmware owner; it does not grant concurrent access to shared FAT.

6. **Keep MTP as staging only and enforce storage ownership.** Packages are uploaded through existing FWU paths, and the coordinator consumes them only when the relevant firmware exclusively owns shared FAT. EF800 Backup recovery MTP and EF800 Application must never mount the shared volume simultaneously. MTP itself does not trigger programming during upload.

7. **Gate post-bootstrap coordination using the running EF800 Application version.** The second-level bootloader remains responsible for installing `EF800_APP.pkg` before handoff and MUST NOT write transaction-journal records because its partition is at the size limit. EF800 Application obtains its own version from the shared version manifest at build time. If `EF800_APP.pkg` remains staged, it validates the package and compares the declared version to its running version; a mismatch stops all later updates. If the package is absent (including after successful bootloader installation and cleanup), the coordinator reports that stage skipped and proceeds. The bootloader's existing serial diagnostics remain the source for its update-specific failure trace.

## Risks / Trade-offs

- [Bootloader replacement can make the unit unbootable if power fails] → defer it until all other images pass; require a proven separate-partition programming path and explicit ST-LINK recovery instructions; do not enable it until hardware interruption tests pass.
- [The 128 KiB EF800 Backup may not have room for package scanning or update code] → keep the coordinator in EF800 Application and keep EF800 Backup MTP limited to staging.
- [Progress metadata can become inconsistent with a partially programmed image] → journal phase transitions, re-read/validate target flash on restart, and never trust a completion bit alone.
- [Keyboard updater refresh can disrupt subsequent keyboard updates] → verify updater version and service availability after refresh before continuing; stop safely on mismatch.
- [MTP and update execution can contend for FAT or MSC ownership] → serialize coordinator startup against MTP/MSC and require filesystem unmount/sync before restart or programming.
- [Current physical keyboard updater entry has documented failures] → treat keyboard update ordering as intended behavior, not qualified capability, until the hardware test matrix passes.

## Migration Plan

1. Add package producer/verification tooling and test malformed, wrong-target, duplicate, and interrupted package sets.
2. Use the legacy `EF800_FW.bin` path once to install EF800 Application 1.0.0 on devices whose deployed bootloader does not understand the package envelope.
3. Implement transaction journaling, package validation, status reporting, and EF800 Application/EF800 Backup update paths while keeping bootloader update disabled.
4. Implement and validate the bootloader update mechanism separately; only enable it after targeted power-loss and recovery tests.
5. Integrate keyboard packages and test each keyboard target independently before enabling full-set progression.
6. Stage the post-bootstrap package set through MTP, validate every completion/status transition, and run the recovery matrix. Preserve the current Programmer procedure as fallback.

Rollback is per image, not atomic across all six. If an update fails, the coordinator stops at the failed stage, preserves uncompleted packages, and reports whether normal boot, retry, EF800 Backup recovery, or ST-LINK service is required.

## Open Questions

- Can the STM32L496 safely program the bootloader partition from EF800 Application with all flash-resident execution, vectors, interrupt handlers, and watchdog behavior accounted for, or is a RAM-resident installer required?
- Should the package envelope use a cryptographic signature in addition to CRC, and what key provisioning/anti-rollback policy applies? Until decided, CRC provides corruption detection only, not authenticity.
- Does absent package mean skip by default, or should a user explicitly authorize a partial update set?
