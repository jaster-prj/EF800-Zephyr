# Coordinate Complete Firmware Updates from Staged Packages

- Status: accepted, supersedes ADR-0013
- Date: 2026-11-13
- Supersedes: ADR-0013

## Context

ADR-0013 established a fixed L496 image map, a bootloader that installs only EF800Application from QSPI FWU, ST-LINK-only bootloader replacement, and no automatic BackupApplication replacement. EF800Application now has target-specific keyboard update machinery, and USB MTP provides completed-file staging in FWU without raw-sector exposure. Updating the complete EF800 requires a durable policy for target identity, validation, ordering, interruption, and recovery across three L496 and three L431 images.

## Decision

EF800 will use a EF800Application-owned coordinator for a staged set of target-identified firmware packages. The Bootloader remains responsible for installing EF800Application at its startup boundary; EF800Application owns BackupApplication and keyboard package updates. EF800 Bootloader replacement is deferred until last and may be enabled only through a mechanism that does not erase code while executing from the bootloader region; if that safety condition cannot be established, its update remains an explicit service operation.

MTP remains a filesystem-mediated staging interface and never exposes raw flash. Packages remain staged until target-specific programming and post-write verification succeed. The fixed EF800 and keyboard flash maps, partition bounds, and QSPI ownership boundaries remain unchanged. Update progress and failure status must survive restarts sufficiently to prevent a partial transaction from being reported as complete.

## Consequences

- The old policy that categorically limits the Bootloader to ST-LINK and excludes BackupApplication from coordinated replacement is replaced by staged-package coordination, subject to safety and hardware qualification gates.
- The update implementation must validate each target independently and maintain per-image status and retry/recovery semantics; a six-image update is not atomic.
- MTP continues to expose only approved filesystem paths and does not grant the host firmware-programming authority.
- The update coordinator and package tooling become compatibility-critical; package format and image compatibility require explicit versioning.
- Bootloader-update safety and power-loss recovery must be demonstrated before enabling that target in production. ST-LINK remains the recovery fallback.
- Existing QSPI partitioning, MTP ownership, and no-concurrent-mount constraints from ADR-0013 remain in force.
