# Automatically Format Recognized Legacy QSPI During Development

- Status: accepted, supersedes the legacy-media handling in ADR-0013
- Date: 2026-10-29
- Supersedes: ADR-0013 (legacy-media handling only)

## Context

The new QSPI map reserves 32 KiB for boot-state NVS, 992 KiB for Configuration
FAT, and 3072 KiB for Shared Data FAT. Older development units may still have
one monolithic FAT volume beginning at offset zero. The active development
target has no data that needs preservation, so requiring a manual flash-fixture
migration blocks iteration. This exception must not make an arbitrary mount
failure permission to format media or silently become a production policy.

## Decision

When `CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI` is enabled, EF800 Application SHALL
check the raw QSPI boot sector before reading boot metadata or mounting
Configuration/Data. If and only if the sector passes the validated legacy FAT
signature check, EF800 Application SHALL format the new Configuration and Shared
Data FAT partitions and erase the boot-state NVS partition before continuing
startup. The old volume contents are intentionally discarded; there is no data
migration. FAT mount failures and malformed NVS records alone SHALL NOT invoke
this formatter.

The development EF800 Application build enables this option. Production builds
SHALL disable it until the fitted QSPI part, capacity, erase geometry, frequency,
and power-loss behavior are qualified. EF800 Bootloader and EF800 Backup remain
non-formatting and keep their legacy-layout safeguards. ADR-0013's partition
map, storage ownership, and MTP boundaries remain unchanged.

## Consequences

- A development unit with a recognized old monolithic volume automatically
  reaches the new partitioned layout on its next EF800 Application boot.
- The old data is destructive-format territory; this option is only suitable
  for development targets where that data is disposable.
- The formatter keeps the legacy boot-sector signature until both FAT
  partitions format, allowing a retry if power fails during either format.
- Physical QSPI qualification and production migration remain open gates; this
  software decision is not evidence that the physical formatter is qualified.
