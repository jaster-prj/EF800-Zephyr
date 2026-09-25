# EF800 Persistence Plan

## Goal

Port the Mbed OS persistence and Workspace behavior to Zephyr without making
the new firmware dependent on native C/C++ struct layout or on an unverified
QSPI device. The persistence layer must be testable on `native_sim` and must
never format a failed filesystem mount.

## Compatibility Contract

The legacy logical layout is retained:

```text
configuration: .workspace, time settings, port assignments, device settings
data:          <workspace-name>.dat
```

On Zephyr these roots are mounted below `/EF800:`:

```text
/EF800:/.configuration/
/EF800:/data/
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
- Versioned/checksummed timezone, port-assignment, boot-metadata, and serial
  configuration records.
- Boot metadata is stored as NVS value ID 1 in the fixed internal-flash
  `metadata_partition`, with NVS data CRC enabled in both images.
- Connection-aware routing, UART ownership validation, console fallback, and
  MainApplication measurement integration.
- Explicit legacy Mbed readers, preflight validation, read-back verification,
  duplicate DebugDevice rejection, and preservation of malformed source data.

The compatibility contract remains normative. Physical QSPI/FAT behavior and a
production Mbed file dump are still open validation tasks.

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
- Keep the USB host disconnected because BackupApplication currently exposes
  the unverified QSPI disk through MSC.

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

### Internal boot metadata storage

The fixed `metadata_partition` at `0x08028000`, size `0x8000`, is an NVS
partition. The complete encoded boot metadata record remains one value under
ID 1; its version, size, sequence, and payload CRC remain part of the record.
NVS supplies record allocation, sector rotation, and recovery of interrupted
garbage collection. The old raw 40-byte record at partition offset zero is not
an NVS record, so a format transition creates default metadata unless a
separate migration preserves it. Both images use the shared NVS reader and no
code reads `0x08028000` directly.

The metadata partition is reserved for small bounded records. Workspace and
measurement data remain on QSPI/FAT when that hardware path is explicitly
enabled.

### Phase 3: QSPI/FAT and USB ownership

The QSPI/FAT and MSC paths are compiled into Bootloader and MainApplication.
Before enabling them on a target, verify:

- Confirm the identified `IS25LP032D-JTLE-TR` against JEDEC ID or SFDP.
- Confirm capacity, erase geometry, program size, and maximum frequency.
- Confirm that the existing FAT volume is readable.
- Verify `.configuration` and `data` directory behavior.
- Verify that mount failure does not format the volume.
- Verify that MainApplication FAT access and backup USB MSC access are mutually
  exclusive.
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
behavior, deletion, list filtering, and storage-unavailable behavior.
