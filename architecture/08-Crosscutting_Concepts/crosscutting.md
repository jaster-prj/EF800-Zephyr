# Crosscutting Concepts

## Startup and dependency ordering

The Main composition root establishes console and shell routing, initializes
the shared foundation, loads persisted composition and assignments, discovers
plugins when Shared Data is mounted, prepares physical devices and USB routing,
then enters its runtime loop. Optional hardware or unavailable configuration
must not prevent unrelated interfaces from starting where the code supports
independent operation.

## Persistence and recovery

- Boot metadata is a bounded versioned record in NVS and is independent of FAT.
- Composition uses validated, generation-numbered dual slots; commit writes and
  verifies the inactive slot, while activation is restart-scoped.
- Workspace and settings use explicit file services and bounded parsers.
- Supported legacy settings migration is distinct from development-only
  whole-volume legacy-layout reformat.
- Normal mount failure does not authorize formatting. Development migration
  requires a recognized legacy boot-sector signature.

## Storage and USB ownership

- Main owns Configuration FAT and Shared Data FAT during normal operation.
- Main MTP exposes named roots only: Data, Plugins, and FWU.
- Backup MTP also exposes Configuration while Backup is the sole active image.
- Neither MTP service exposes NVS, internal flash, arbitrary mount roots, or raw
  sectors.
- Physical MSC is a separate raw-disk ownership mode; FAT must be unmounted
  first. Main and Backup must not mount the same shared disk simultaneously.
- MTP upload publication is staged and only reports completion after the
  transfer is complete and synchronized.

## External package boundary

The PluginManager adapter scans `.PMP` files under Shared Data `plugins`, checks
package/contract compatibility, and exposes EF800 measurement/calculation
services. Main owns hardware, persistence, port transport, host UI, and lifecycle.
Native-sim and ARM packages have distinct ABIs. The MPU/user-mode containment
intent is not proof of target qualification; MPU layout, fault attribution,
deadline behavior, and cleanup must be verified on STM32L496.

## Device abstraction and native simulation

Production domain services use Zephyr device, GPIO, UART, I2C, RTC, flash, file
system, and USB APIs. Native-sim supplies emulators and virtual peers at those
seams while compiling the shared EF800 Application sources. Simulated behavior is
useful for control-flow and protocol tests, but does not model all STM32 register
effects, signal timing, or board electrical characteristics.

## Error handling and observability

Subsystems report errors through Zephyr logging, console/shell, UI status, and
bounded virtual diagnostics. Missing or invalid optional data generally results
in repository defaults and a diagnostic rather than an implicit destructive
repair. Bootloader image rejection and watchdog recovery have explicit fallback
paths. Console routing is configured from persisted assignments when available.

## Security and trust boundaries

MTP roots and paths are restricted; gateway operations are bounded by
relative-path checks and transfer limits. Plugin contracts restrict interfaces
but do not by themselves establish authenticity or execution safety. Physical
debug access, package signing/anti-rollback, and validated hardware isolation
remain separate concerns and release gates.
