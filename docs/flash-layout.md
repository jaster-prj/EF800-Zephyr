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

The backup image was historically allocated `0x20000`, but the next image
starts at `0x08030000`; the unused half is therefore represented explicitly as
reserved space. This must be confirmed against the final backup image size and
STM32L4 erase geometry.

## External QSPI flash

The legacy firmware exposes the external QSPI NOR as a FAT volume. The fitted
NOR part and capacity are not identified in the source repository. Set the
`size` property in the EF800 board DTS from the schematic or JEDEC/SFDP data
before enabling production storage or update packages.
