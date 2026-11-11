# EF800 Zephyr migration status

This file is the current closeout summary for the Mbed OS to Zephyr port.
Detailed subsystem notes and hardware qualifications remain in the linked
documents; this summary does not imply production qualification of unverified
hardware assumptions.

## External SRAM and plugin isolation

- The EF800 board uses a validated 512 KiB FMC SRAM region at `0x60000000`.
  MainApplication validates SRAM before pool use and selects an internal-only
  fallback when validation fails.
- MainApplication external allocations are explicit. The plugin text/rodata
  arenas and two 4 KiB calculation scratch buffers are external-first with
  internal fallbacks. Worker/supervisor stacks, mailbox and syscall staging
  remain internal. See `external-sram-memory-baseline.md`.
- Plugin metadata v4 declares bounded text, rodata, data, BSS, worker-stack,
  mailbox and plugin-owned shared-buffer requirements. The package verifier and
  MainApplication check section sizes, aggregate writable memory, alignment,
  active arena capacities, and worker MPU layout before accepting a descriptor.
- On-target external-SRAM validation, 72 KiB requested pool accounting,
  discovery of three plugin packages, and calculation-plugin execution have
  been observed. The fault-injected internal-only image also completed core
  storage, USB, LCD, keyboard, plugin discovery, memory diagnostic and
  calculation execution checks.
- The normal MainApplication image is flashed at `0x08030000`; target startup
  validates SRAM and discovers all three packages. Investigation found the
  installed ELTA3/FTS3XX descriptor extensions are 84 bytes (metadata v3),
  while the host requires 104-byte metadata v4. The registry previously failed
  to transition an already-registered package to INCOMPATIBLE, leaving zero
  serial defaults marked READY and causing UART setup to fail with `-ENODEV`.
  Diagnostic-state updates now apply to existing entries. Fresh v4 ELTA3,
  FTS3XX, and calculation packages build and verify, but the target's QSPI
  package copies have not yet been replaced; target instrument protocol
  operation therefore remains unverified and the package replacement is a
  prerequisite for that acceptance test. The updated loader was flashed at
  `0x08030000` and now explicitly rejects those v3 packages (`extension
  missing/truncated: size=84`, bind `-ENOTSUP`) rather than attempting UART
  initialization with zero serial settings. MTP package replacement could not
  be performed in this session because the host reports no raw MTP device.
- This evidence is not a complete MPU/fault/timeout/recovery acceptance matrix.
  Treat plugin isolation and SRAM use as bring-up validated, not release
  qualified, until that matrix is run and recorded.

## Regression baseline

- Native tests: 174/174 cases pass (26 configurations).
- Four firmware targets build: EF800 backup, bootloader, keyboard, and keyboard
  bootloader; MainApplication also builds.
- Image layout verifier reports bootloader at `0x08000000`, backup at
  `0x08010000`, and MainApplication at `0x08030000`, within their fixed slots.

## Outstanding hardware gates

Complete the L496 MPU permissions and denied-access checks, induced plugin
fault/timeout/recovery and resource-cleanup tests, and repeat the acceptance
matrix against the normal non-fault-injected image. Maintain the existing
release gates for QSPI part/geometry qualification and shared FAT/USB ownership.
