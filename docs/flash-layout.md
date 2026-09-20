# EF800 flash layout

## Internal flash

The STM32L496ZGT6 contains 1 MiB of internal flash. The existing Mbed images
use these fixed addresses:

| Partition | Address | Size |
|---|---:|---:|
| Second-level bootloader | `0x08000000` | `0x10000` |
| Backup application | `0x08010000` | `0x10000` |
| Reserved metadata/gap | `0x08020000` | `0x10000` |
| Main application | `0x08030000` | `0xD0000` |

The current Zephyr contract deliberately assigns `0x10000` to the backup image
and `0x10000` to boot metadata. The captured legacy `BackupApplication.bin` is
`0x20000` bytes and has its reset vector at `0x08024AE5`, so the legacy image
uses the upper range now assigned to metadata. Do not treat
`0x08020000..0x0802FFFF` as unused when restoring the legacy capture. The
smaller Zephyr backup partition remains intentional and must fit within the
current fixed image contract.

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

The hardware-debug image provides explicit raw-flash probes and a destructive
FAT/MSC ownership test. The storage test is enabled only in the temporary
debug image; it formats the QSPI volume after `format-confirm`, creates test
files, switches ownership between internal FAT and USB MSC, and verifies a
file created by the host. Production storage and update packages remain
disabled until this test and the normal-image ownership tests pass.
