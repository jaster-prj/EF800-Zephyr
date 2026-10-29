# EF800 flash layout

## Internal flash

The STM32L496ZGT6 contains 1 MiB of internal flash. The Zephyr image contract
uses three non-overlapping regions:

| Partition | Address | Size |
|---|---:|---:|
| Second-level bootloader | `0x08000000` | `0x10000` |
| Backup application | `0x08010000` | `0x20000` |
| Main application | `0x08030000` | `0xD0000` |

The 64 KiB bootloader, 128 KiB BackupApplication, and 832 KiB MainApplication
cover the 1 MiB device. MainApplication remains linked at `0x08030000`. Boot
state is stored in the dedicated external-QSPI NVS partition, not in internal
flash. The legacy `BackupApplication.bin` capture is reference evidence only
and must not be flashed into the current image layout.

## External QSPI flash

The fitted external QSPI NOR is an ISSI `IS25LP032D-JTLE-TR`, powered from the
same 3.3 V rail as the STM32. It is directly connected to QSPI Bank 1:

| Signal | STM32 pin |
|---|---|
| IO0 | PF8 |
| IO1 | PF9 |
| IO2/WP# | PF7 |
| IO3/HOLD#/RESET# | PF6 |
| CLK | PF10 |
| NCS# | PE11 |

The device capacity is 32 Mbit / 4 MiB. The temporary hardware-debug overlay
uses STM32 internal pull-ups on `NCS#`, `IO2/WP#`, and
`IO3/HOLD#/RESET#`; the PCB has no external pull-ups and these lines float
while the STM32 is in reset. External pull-ups close to the flash are
recommended for production stability: approximately 4.7 kOhm to 10 kOhm from
each of those three lines to 3V3. Do not add pull-downs to them. The same
startup biasing must be applied to the normal image overlays before relying on
QSPI across reset and boot handoff.

The software partition contract covers the device address space exactly:

| Region | QSPI offset | Size | Purpose |
|---|---:|---:|---|
| Boot-state NVS | `0x000000` | `0x008000` (32 KiB) | Boot selection/failure metadata only |
| Configuration FAT | `0x008000` | `0x0F8000` (992 KiB) | `.configuration`, assignments, profiles, time and serial settings |
| Shared-data FAT | `0x100000` | `0x300000` (3072 KiB) | `data`, `plugins`, and `fwu` |

These are firmware offsets, not evidence that the fitted QSPI geometry, erase
behavior, existing media, or power-loss behavior has been production-qualified.
The legacy monolithic FAT volume is never automatically repartitioned or
formatted; see `docs/qspi-migration.md`.

The hardware-debug image provides explicit raw-flash probes and a destructive
FAT/MSC ownership test. The storage test is enabled only in the temporary
debug image; it formats the QSPI volume after `format-confirm`, creates test
files, switches ownership between internal FAT and USB MSC, and verifies a
file created by the host. This temporary destructive test does not qualify
the production partitioned volumes. Physical QSPI qualification and target
programming remain release gates.
