# PluginManager Test Environment

The host targets the ST NUCLEO-G0B1RE (STM32G0B1, Cortex-M0+) on Zephyr
`v4.4.1`. USART2 is the NUCLEO ST-LINK console at `115200 8N1`; USART1 on the
Arduino serial pins is reserved for the selected measurement instrument.

Observed host connections: the NUCLEO ST-LINK USART2 console enumerates as
`COM14` (`115200 8N1`); the CP210x USART1 device link enumerates as `COM7`.
Close other serial applications before using either port. This is the physical
mapping used by all current acceptance tests.

## Current Verification (2026-10-04)

The current flashed image passes the following on the NUCLEO-G0B1RE:

- FAT volume mounted without formatting: `mounted=yes attached=yes`.
- Root packages `/PM:/elta3.pmp` and `/PM:/fts3xx.pmp` validated with the
  streaming reader and image CRC.
- ELTA3 dummy measurement through COM7: `1200 7O2`, pass.
- Fennel-compatible FTS3XX dummy measurement through COM7: `9600 8N1`, pass.
- ELTA3 to FTS3XX switching without reset, pass.
- Plugin discovery, load, instantiate, destroy, unload, and calculation smoke
  tests, pass.

The application owns UART reset, prescaler, clock policy, and receive-byte
normalization. Plugins provide the serial communication parameters only.

This proves the `pluginmanager-test` integration path. It does not qualify the
production EF800 MainApplication, native-code trust, executable-memory
isolation, update/rollback behavior, or USB MSC storage ownership.

The flashed image uses this known-working physical mapping. The inverse logical
labels were tested and produced no console output, so they are not used.

On 2026-10-01, after correcting the debugger connection, ST-LINK serial
`0667FF515754888367135124` detected the STM32G0B1 as device ID `0x467`.
The test image at `build/pluginmanager-test/zephyr/zephyr.bin` flashed and
verified at `0x08000000`; the COM14 shell started normally. The NOR driver
reported a 64 KiB erase page, requiring a 64 KiB flashdisk cache. Read-only
inspection of the existing boot sector showed a 4096-byte FAT sector size,
which is now reflected in the overlay and FatFS configuration. FAT still
rejects the existing volume with `-ENODEV` (`FR_NO_FILESYSTEM`). Its boot
sector declares 1024 sectors (4 MiB), while the flashdisk spans 8 MiB. Do
not format or overwrite the volume based on this evidence. The host stays
operational in degraded mode with storage actions disabled.

Further read-only checks show sector 1 begins `f8 ff ff 00 f0 ff` (plausible
FAT entries); the volume boot sector begins `eb fe 90` and has a `55 aa`
signature at bytes 510-511. A 4 MiB FAT volume can reside on an 8 MiB disk;
the volume/disk size difference alone does not explain the mount failure.
Logical FAT sectors (4 KiB) and physical erase blocks (64 KiB) are different
units, both supported by the current disk settings. Winbond's W25Q128JV
datasheet describes a 128-Mbit (16 MiB) device, with 4 KiB sector erase and
64 KiB block erase. This board overlay instead declares `ef 40 16` (64 Mbit /
8 MiB). The current Zephyr `CONFIG_SPI_NOR_SFDP_MINIMAL` driver reads the
chip's JEDEC ID at startup and rejects a mismatch with that overlay; the NOR
disk initializes and returns data, which is evidence for `ef 40 16` on this
connection rather than the W25Q128JV (`ef 40 18`). This does not identify a
part from its marking or validate the entire flash contents. Do not change
the overlay capacity to 16 MiB based on the supplied datasheet alone. FAT
still returns `FR_NO_FILESYSTEM` with the observed sectors.

Historical physical results before root-volume storage support:

```text
plugin status         mounted=no attached=no loaded=no instance=no selected=0
plugin menu           ELTA3 (plugin 2, port 2): package missing
calc status           Offset calculation: ready
calc demo 5           105 / -15 (built-in sample 100 / -20)
calc preview 5        calculation unavailable: -19
plugin assign 3 2     rejected: port 2 already assigned
plugin assign 2 0     succeeded; menu no longer lists ELTA3
plugin assign 3 2     succeeded; FTS3XX shown as package missing
plugin select 3       rejected: package unavailable
```

No package-loaded Measurement execution or FAT-backed Calculation preview/save
has been demonstrated on this board. `calc demo` is memory-only and does not
read or write the NOR. The Arduino USART1 instrument bridge is COM7; it was
not exercised during this storage-blocked pass.

Package files are now expected at the FAT root (`/PM:/elta3.pmp` and
`/PM:/fts3xx.pmp`) rather than under `/PM:/plugins`. This avoids the target
FatFS directory-name limitation reported during bring-up.

The storage-backed milestone image was flashed on 2026-10-04 and the root-volume
package lifecycle and reader validation passed. No formatting, package
installation, or external-volume write was performed.

COM7 retry on 2026-10-04: the Windows dummy received the ELTA3 request
`S14 0d` and logged transmission of `A 0d 0a`, but the NUCLEO measurement call
still returned `-110` (device read timeout). This proves the NUCLEO TX path to
the CP210x is active and isolates the remaining issue to the simulator-to-board
RX path, UART wiring, signal levels, or RX pin configuration. The flashed image
was not changed after this test.

## External SPI NOR

| Signal | Arduino pin | NUCLEO-G0B1RE pin |
| --- | --- | --- |
| SCK | D13 | PA5 |
| MISO | D12 | PA6 |
| MOSI | D11 | PA7 |
| CS | D10 | PB0 |
| RESET | D9 | PC7 |
| WP | D8 | PA9 |

The tested JEDEC ID is `ef 40 16`, a W25Q64-class 64 Mbit / 8 MiB device. The
overlay uses 24 MHz SPI and exposes the full device as the `PM` disk. The
current bring-up image erases the full partition, formats it as FAT, mounts it,
and validates dummy-file write/read/remove operations on every boot. This is
destructive and must not be used with production data.

## Build And Flash

```powershell
docker start EF800_Devcontainer
docker exec -w /workspace EF800_Devcontainer `
  /opt/venv/bin/west build -p always -b nucleo_g0b1re `
  application/application/pluginmanager-test `
  -d application/build/pluginmanager-test `
  -DPLUGIN_MANAGER_DIR=/workspace/PluginManager
```

Generated firmware is under
`application/build/pluginmanager-test/zephyr/`. The connected test ST-LINK has
serial `0667FF515754888367135124`:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" `
  -c port=SWD SN=0667FF515754888367135124 `
  -w "C:\Users\u-45625\Programmierung\zephyr\EF800\application\build\pluginmanager-test\zephyr\zephyr.bin" 0x08000000 `
  -v -rst
```

The programmer must identify device ID `0x467` and verify the download.

## Runtime Check

Startup should report:

```text
PluginManager NUCLEO host starting
PluginManager host ready; use 'plugin list'
```

Run `plugin list`, connect the matching instrument or SDK simulator to USART1,
then run `plugin select 2` or `plugin select 3`, `port status`, and `kb measure`.
The loaded instance is cast to `struct ef800_measurement_instance`, and its API
version, size, and required function pointers are validated before direct typed
calls. The SDK Port UUID remains in the bound descriptor and is satisfied by
the persistent USART1 service.

Plugin text executes from internal RAM. This physical integration host uses
trusted packages and does not provide fault recovery or an execution timeout.
