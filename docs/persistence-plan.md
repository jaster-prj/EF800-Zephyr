# EF800 Persistence Plan

## Goal

Port the Mbed OS persistence and Workspace behavior to Zephyr without making
the new firmware dependent on native C/C++ struct layout or on an unverified
QSPI device. The persistence layer must be testable on `native_sim` and must
never format a failed filesystem mount.

## Compatibility Contract

The legacy logical settings and Workspace behavior are retained, but their new
storage ownership is split:

```text
Configuration FAT: .configuration, time settings, port assignments,
                   serial settings, target composition, and workspace selection
Shared Data FAT:    <workspace-name>.dat, data, plugins, and fwu
```

On Zephyr the filesystem roots are:

```text
/EF800:/.configuration/
/DATA:/data/
/DATA:/plugins/
/DATA:/fwu/
```

Workspace files use the `.dat` suffix. The `.workspace` file contains the
selected filename, with optional trailing CR/LF accepted for compatibility with
the legacy implementation.

New persistence records must use an explicitly defined, versioned format. The
Mbed `Persistence<T>` files contain native `int`, `bool`, enum, pointer, and
padding representations and are not portable between toolchains. Zephyr must
not write new records in that format.

Existing Mbed records are read-only migration inputs. A compatibility reader is
required only if deployed Mbed settings must survive the firmware migration.
The migration reader must validate file length, record count, IDs, and bounds
before accepting any record.

## Completed migration scope

The following persistence and Workspace work is complete in software and is
covered by the native fake backend and firmware builds. It is not an active
implementation plan item:

- Backend-neutral persistence API with path/name validation and a no-storage
  Zephyr adapter.
- Native fake backend and Workspace create, select, restore, list, and append
  behavior.
- MainApplication Workspace and Position UI, including storage-unavailable
  handling and fixed-point millimetre records.
- Bounded row scanning and selection for `Show Measurements`, raw-text editing
  capped at 255 bytes, confirmed deletion of any row, and CR/LF/CRLF ending
  preservation.
- Same-volume staged Workspace replacement with alternating checksummed
  recovery-journal slots, backup/rollback, bounded streaming, startup recovery,
  and MTP source-version conflict checks.
- Versioned/checksummed timezone, port-assignment, boot-metadata, and serial
  configuration records.
- Boot metadata is stored as NVS value ID 1 in the 32 KiB external-QSPI
  boot-state partition, with NVS data CRC enabled in Bootloader and
  MainApplication. Internal flash contains only the 64 KiB Bootloader,
  128 KiB BackupApplication, and 832 KiB MainApplication regions.
- Port assignments and composition profiles stay in Configuration FAT; they
  are not mirrored into boot-state NVS.
- Connection-aware routing, UART ownership validation, console fallback, and
  MainApplication measurement integration.
- Explicit legacy Mbed readers, preflight validation, read-back verification,
  duplicate DebugDevice rejection, and preservation of malformed source data.

The compatibility contract remains normative. Physical QSPI partition geometry,
existing-media migration, power-loss behavior, and a production Mbed file dump
remain open validation tasks. A failed mount never authorizes formatting or
in-place repartitioning.

Workspace edits use optimistic MTP conflict detection: the source is checked
immediately before promotion, but the pinned MTP implementation has no shared
mutation gate, leaving a narrow race after that check. Zephyr FAT `fs_sync`
flushes file contents but provides no directory-sync operation, so native
transaction recovery tests do not qualify rename metadata durability during a
physical power loss. Keep that behavior release-gated until target qualification.

## Active plan: controlled hardware and persistence validation

### Phase 1: Target preparation and first flash

- Confirm the fitted QSPI NOR by schematic, JEDEC ID, or SFDP before writing it.
- Confirm capacity, erase geometry, program size, maximum frequency, voltage,
  pinctrl, and existing FAT volume behavior.
- Save internal flash, option bytes, and available QSPI contents before
  changing one recoverable target.
- Build all four images and verify their fixed addresses and sizes.
- For initial QSPI diagnosis, build and verify the temporary `ef800-hwdebug`
  image and flash it at `0x08000000` on one recoverable target. It provides USB
  CDC and read-only JEDEC/SFDP/raw-read commands without replacing the normal
  image set permanently.
- After the debug gate passes, flash the normal bootloader at `0x08000000`,
  BackupApplication at `0x08010000`, and MainApplication at `0x08030000`; flash
  the keyboard controller separately.
- Do not use MSC/raw-sector access for recovery volumes. MainApplication and
  BackupApplication expose approved filesystem roots through separate MTP
  personalities; the shared-data FAT still has one filesystem owner at a time.
  Physical QSPI qualification remains required before using this path on a
  production target.

The first connected STM32L496 engineering target was handled separately from
the normal first-flash procedure: its legacy captures were checked, its option
bytes were recorded, and its complete 1 MiB internal flash was explicitly
erased. It was later used for controlled bootloader/MainApplication bring-up.
The erase is historical evidence; see `docs/stm32l496-target-snapshot.md` and
the current state in `docs/migration-status.md`.

### Phase 2: Hardware bring-up before more hardware-dependent features

- Bootloader handoff, IWDG feeding, MCP23017, DS3231, T6963 initialization,
  keyboard reset/RX startup, and normal MainApplication keyboard parsing have
  been exercised on the recoverable target.
- The physical matrix, all arrow keys, full watchdog fallback accounting, fuel
  gauge, T6963 electrical timing/polarity, status LED behavior, and confirmed
  UART routes remain hardware gates.
- Capture complete logs and resolve the fixed USART1 shell versus dynamic
  external console/log routing behavior.
- Confirm Port 2 flow-control pins, Port 3 alternate modes, and BlueNRG SPI1
  resources from the schematic before adding or enabling their DeviceTree
  descriptions.

### External-QSPI boot metadata storage

The fixed QSPI partition at offset `0x000000`, size `0x008000`, is the NVS
boot-state store. The complete encoded record remains one value under ID 1; its
version, size, sequence, and payload CRC remain part of the record. NVS supplies
record allocation, sector rotation, and recovery of interrupted garbage
collection. Missing or invalid boot-state records reset only this NVS region;
FAT volumes are never formatted as a side effect. Bootloader and MainApplication
use the shared NVS reader, and no code reads the former internal metadata
address directly.

The boot-state partition is reserved for small bounded records. Configuration
records remain on Configuration FAT; Workspace and user data remain on Shared
Data FAT.

### Phase 3: QSPI/FAT and USB ownership

The QSPI NVS and both FAT volumes are compiled into Bootloader,
BackupApplication, and MainApplication. Backup recovery uses file-level MTP, not
raw MSC, for mounted filesystems. Before using these paths on a target, verify:

- Confirm the identified `IS25LP032D-JTLE-TR` against JEDEC ID or SFDP.
- Confirm capacity, erase geometry, program size, and maximum frequency.
- Confirm that the existing FAT volume is readable.
- Verify Configuration FAT at `0x008000` and Shared Data FAT at `0x100000`,
  including `.configuration`, `data`, `plugins`, and `fwu` directory behavior.
- Verify that mount failure does not format the volume.
- Verify that Configuration/Data FAT access is through the owning application
  and approved MTP roots; never expose NVS or raw flash sectors.
- Exercise power loss around writes, appends, metadata updates, and deletes.

### Phase 4: Production data migration

- Capture persistence files from a deployed Mbed device.
- Validate enum, padding, record length, and toolchain-specific representation
  against the real production image.
- Run conversion on a copy first and confirm that malformed, conflicting, and
  partially converted data remains available for diagnosis.
- Validate Workspace and Position behavior with migrated data on physical QSPI.

### Phase 5: Release and update policy

- Define image signing, package authentication, rollback, and recovery policy.
- Keep NVS metadata updates resilient to interrupted sector garbage collection;
  validate the boot-failure counter and fallback behavior on target.
- Keep USB MSD persistence last because it shares the QSPI ownership boundary.
- Repeat native tests, all four firmware builds, `scripts/verify-images.sh`,
  hardware regression tests, and update-package checks before release.

## Verification Gates

Every phase must pass:

- native persistence and Workspace tests
- the existing keyboard/protocol/RTC tests
- all four EF800 firmware builds
- `scripts/verify-images.sh`

Persistence tests must cover missing files, malformed `.workspace`, CR/LF
compatibility, invalid names, path traversal, duplicate creation, append
behavior, deletion, list filtering, and storage-unavailable behavior. Workspace
row tests also cover mixed CR/LF/CRLF endings, empty and unterminated rows,
255-byte edit limits, streaming large-file replacements, interrupted journal
phases, MTP-visible artifact tampering, optimistic conflicts, and measurement
context refresh.
