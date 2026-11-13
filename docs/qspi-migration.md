# EF800 QSPI layout migration

## Development behavior

The new QSPI layout is:

| Region | Offset | Size | Purpose |
|---|---:|---:|---|
| Boot-state NVS | `0x000000` | 32 KiB | Boot selection/failure metadata |
| Configuration FAT | `0x008000` | 992 KiB | Private EF800 configuration |
| Shared Data FAT | `0x100000` | 3072 KiB | `data`, `plugins`, and `fwu` |

The development EF800Application enables
`CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI=y`. Before it reads boot metadata or
mounts storage, it checks QSPI offset zero for a validated legacy monolithic
FAT boot sector. **If that signature is found, the firmware formats the new
Configuration and Shared Data FAT partitions, verifies both raw FAT boot
sectors, erases the boot-state NVS partition, then proceeds with normal
initialization.** The optional `CONFIG_EF800_APPLICATION_FORMAT_BLANK_QSPI` boot-time
provisioning is disabled by default on the board; use the explicit maintenance
command below for an erased/unformatted device. These development paths
discard prior contents; there is no file migration. Keep the legacy option
disabled for production firmware.

The formatter leaves the old boot-sector signature intact until both FAT
volumes format successfully. If power fails during either FAT format, the next
boot can recognize the old layout and retry. The boot-state partition is erased
last, after the new FAT volumes have been created. A normal FAT mount failure
or invalid NVS record by itself does **not** trigger this formatter.

The Bootloader and BackupApplication remain non-formatting: when they encounter
the old layout, the Bootloader skips its update scan and BackupApplication does
not start recovery MTP. The normal boot path must reach EF800Application for the
development migration to run.

Use the EF800Application maintenance command for a full-layout format:

```text
storage format-qspi
storage format-qspi confirm
```

The first command prints a destructive warning; the second must be entered
within 30 seconds. It refuses to run while USB MSC owns the disk, stops other
USB storage personalities, unmounts both FAT volumes, erases all three fixed
QSPI partitions, creates the two FAT volumes, verifies their raw boot sectors,
seeds and verifies the BATSOC0/LCDCONTR1 COMPOSITION slots, erases boot-state
NVS, then reboots. BackupApplication intentionally remains non-formatting while
its recovery MTP service may own the volumes.

## Production and data-preservation gate

Disable `CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI` and
`CONFIG_EF800_APPLICATION_FORMAT_BLANK_QSPI` for production firmware. Do not enable the
legacy formatter on a device whose old files must be retained. Physical QSPI capacity,
erase geometry, program behavior, maximum frequency, fitted part identity, and
power-loss behavior remain release-gated; the firmware-side signature check is
not hardware qualification. The physical QSPI program path is not yet qualified;
see the latest bring-up results below.

## Latest hardware bring-up (2026-10-31)

The fitted device was identified by JEDEC as IS25LP032D (`9D 60 16`, 4 MiB),
and its SFDP signature was valid. Under a guarded hwdebug operation, all three
partitions were erased, but FAT creation did not produce valid boot sectors.
`fs_mkfs()` returned success while raw reads still showed erased data. A bounded
program probe also returned success but read back corrupted bytes at the
Configuration partition start. The status register read `0x40` (QE set, WIP
clear); erase and read operations were observed, but QSPI programming remains
unqualified.

The previous EF800Application boot logged `fs mount error (-19)` because an
erased/unformatted layout was not provisioned. A development-only blank-layout
formatter was tested: both `fs_mkfs()` calls returned success, but **both raw
FAT boot-sector checks failed**; provisioning returned `-5`, and mounts were
skipped. Boot-state NVS also reported an invalid CRC. The boot-time blank
formatter is now disabled on the physical board to prevent repeated attempts.
EF800Application has an explicit `storage format-qspi confirm` maintenance
command. It was run from EF800Application: both FAT partitions were erased, but
both raw boot-sector checks failed (`-5`, I/O verification failure). The
boot-state NVS erase is deliberately deferred until FAT verification succeeds,
so NVS was not erased by that failed attempt.

A temporary diagnostic compared the Zephyr flash API, raw SPI `0x02` Page
Program, and quad `0x32` Page Program on one 4-KiB sector. That test was
inconclusive: the raw 1-1-1 read returned all `0x00` even after erase, while
1-4-4 returned `0xFF`; the Zephyr API read buffer also remained unchanged. Do
not use those raw-test results to diagnose program behavior. The later GPIO
bit-bang read, which bypassed the STM32 QSPI peripheral, agreed with the Zephyr
API for both FAT headers after the flash replacement.

After the external flash was replaced and soldered, the EF800Application
maintenance command was run again. Both FAT boot-sector checks passed, and the
subsequent EF800Application boot mounted Configuration and Shared Data. The
Zephyr flash API and GPIO bit-bang SPI reads agreed on the FAT headers at
`0x008000` and `0x100000`. EF800Application loaded COMPOSITION generation 2
(`BATSOC0`/`LCDCONTR1`) and reported BATSOC0 active. The normal
Bootloader/BackupApplication/EF800Application images are installed; the
DeviceTree partition map is unchanged. The earlier raw QSPI program diagnostic
was not trustworthy for 1-1-1 reads and should not overturn the successful
replacement-flash verification.

If legacy data needs to be preserved, do not boot an image with this option
enabled. First make and verify a full QSPI backup and a file-level backup, then
use a separately qualified migration procedure. Legacy firmware must not be
booted against the new partition layout because it expects a single FAT volume
beginning at offset zero.

## Full-format and MTP confirmation (2026-11-08)

On the development target, `storage format-qspi confirm` erased all 4 MiB and
reported both Configuration and Shared Data raw FAT boot sectors verified;
COMPOSITION `BATSOC0/LCDCONTR1` was also verified. EF800Application subsequently
mounted both volumes. As expected, the format reset Configuration data,
including the Port 4 USB assignment. Port 4 was restored to USB MTP, the target
enumerated as `2fe3:ef87`, and the three MTP roots exposed Data, Plugins, and
FWU. Empty `.data`, `.plugins`, and `.fmu` markers were created in those roots.
This records successful formatter behavior on this target, not production
qualification of QSPI capacity, geometry, frequency, or power-loss behavior.
