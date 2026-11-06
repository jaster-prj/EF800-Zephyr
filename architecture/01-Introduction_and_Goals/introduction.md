# Introduction and Goals

EF800 is a measurement instrument being migrated from Mbed OS to Zephyr. The
firmware preserves existing instrument workflows while replacing its runtime,
drivers, persistence services, update flow, and USB integration. The repository
also contains an independent STM32L431 keyboard controller and a `native_sim`
behavioral target for application and browser-based development.

## Primary goals

1. Preserve user-visible measurement, keyboard, LCD, configuration, workspace,
   and update behavior while moving to Zephyr.
2. Keep a fixed internal-flash image contract and offer a one-boot Backup
   recovery path when MainApplication repeatedly fails.
3. Keep boot-state metadata, private configuration, and shared files in distinct
   QSPI partitions with explicit filesystem and USB ownership.
4. Provide file-level USB MTP access without exposing raw boot metadata or
   arbitrary flash sectors.
5. Reuse production MainApplication orchestration in `native_sim` for behavioral
   testing without claiming STM32 or board-level equivalence.
6. Make developer builds and tests reproducible through the pinned west
   workspace and reusable development container.

## Scope boundaries

- The project is not a register-level STM32 emulator.
- Native simulation does not qualify QSPI geometry, electrical timing, USB-host
  interoperability, MPU behavior, or physical wiring.
- Development-only legacy-QSPI formatting is not a production migration or a
  data-preserving converter.
- BackupApplication is a recovery environment, not a second normal application
  or an independently updated image.
- The keyboard controller is separately built and flashed; it is not part of
  the L496 firmware image.

See [`../../docs/testing.md`](../../docs/testing.md) for validation boundaries
and [`../../docs/qspi-migration.md`](../../docs/qspi-migration.md) for QSPI
qualification gates.
