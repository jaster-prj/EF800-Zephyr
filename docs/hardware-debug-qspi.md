# EF800 external flash hardware debug image

`ef800-hwdebug` is a temporary, read-only external-flash bring-up image. It replaces
the complete internal flash image at `0x08000000` on one recoverable engineering
target. It is not part of the normal four-image boot chain and must be replaced
before returning the target to normal firmware.

## Scope

The image provides:

- USB CDC-ACM as a small line-oriented diagnostic command console.
- Read-only SPI, QSPI, and QPI JEDEC-ID and memory-read test cases.
- Explicit recovery attempts for an externally unknown flash protocol state.
- Internal STM32 pull-ups on `NCS#`, `IO2/WP#`, and `IO3/RESET#/HOLD#`.

The PCB currently has no external pull-ups. For production stability, add
approximately 4.7 kOhm to 10 kOhm from `NCS#`, `IO2/WP#`, and
`IO3/HOLD#/RESET#` to 3V3, placed close to the flash. Do not add pull-downs to
these lines. The internal pull-ups are inactive while the STM32 is in reset.

All three protocol families use the STM32L496 `QUADSPI` peripheral on Bank 1.
They differ in the number of active lines in each flash transaction:

| Protocol | Transaction format | Test commands |
|---|---|---|
| SPI | `1-1-1` | `9F`, `05`, `5A`, `03`, `0B` |
| QSPI | `1-1-4`, `1-4-4` | `6B`, `EB` |
| QPI | `4-4-4` | `35`, `AF`, `9F`, `0B`, `EB`, `F5` |

`SPI` in this table means the standard serial-flash protocol, not a separate
STM32 SPI peripheral. The board has no separate SPI peripheral connected to
the external flash pins.

The image does not provide:

- FAT or filesystem access.
- USB MSC.
- Bootloader update installation.
- Flash erase, program, or write shell commands.
- Automatic Quad Enable register programming.

The external-flash description is for the fitted `IS25LP032D-JTLE-TR`: 32 Mbit,
4 MiB, 3.3 V, directly connected to QSPI Bank 1:

| Signal | STM32 pin |
|---|---|
| IO0 | PF8 |
| IO1 | PF9 |
| IO2/WP# | PF7 |
| IO3/HOLD#/RESET# | PF6 |
| CLK | PF10 |
| NCS# | PE11 |

## Build

Run west inside the reusable development container:

```powershell
docker start EF800_Devcontainer
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-hwdebug -d application/build/ef800-hwdebug
docker exec -w /workspace EF800_Devcontainer bash application/scripts/verify-hwdebug-image.sh
```

The checker requires `rom_start=08000000` and an image no larger than 1 MiB.

## Flash and recovery

Before flashing:

1. Use only a sacrificial or fully recoverable target with SWD access.
2. Save the existing internal flash, option bytes, and QSPI contents if
   possible.
3. Disconnect the USB host during programming.
4. Flash only the debug image at `0x08000000`. The temporary image may occupy
   the complete 1 MiB internal flash; do not use the normal image addresses
   while it is installed.

The debug image overwrites the normal four-image internal-flash layout. Restore
the normal bootloader, backup, and main images before using the device as an
EF800 system again.

The recorded engineering target uses a hardware-selected independent watchdog.
This image configures and feeds the watchdog while waiting for CDC DTR and
while running commands. If the console resets repeatedly, capture the reset
cause with SWD and verify the target option bytes before changing flash
transactions.

## USB console

Connect the USB host after programming and reset the target as required by the
programmer. The device enumerates as `Jaster EF800 HW Debug` using temporary
engineering USB identifiers. Open the assigned CDC serial port and assert DTR.
The diagnostic command console is available after DTR is asserted.

The external flash may remain powered throughout MCU programming and reset.
Do not assume that an MCU reset also reset the flash. Start with `flash probe`
to observe the current state without sending reset or mode-changing commands.

If the board contains a USB-select switch controlled by MCP23017 pin 8, CDC
enumeration also requires that switch to select the STM32 USB path. The
standalone image does not change MCP23017 outputs because the switch polarity
is not yet confirmed. Verify the USB path and switch state from the schematic
before treating a missing CDC device as a firmware failure.

## Console commands

Offsets are zero-based offsets within the external 4 MiB flash. All commands
are read-only and limit one dump to 256 bytes.

```text
flash info
flash probe
flash recover
flash jedec
flash sfdp
flash sfdp 0 256
flash read 0 64
flash read 0x1000 128

flash test spi
flash test qspi-1line
flash test qspi-4line
flash test qspi
flash test qpi
flash test all
```

The `qspi` command name remains accepted as an alias for `flash`, so existing
commands such as `qspi read 0 64` continue to work.

`flash probe` only attempts read transactions in the current state. It does
not attempt recovery and does not intentionally change the flash protocol.
`flash recover` tries QPI and SPI release/reset framings and confirms the final
state with a SPI JEDEC transaction.

The recovery steps are implemented separately for the before-test and
after-test boundaries. A failed before-test recovery is reported as
`unconfirmed`; the testcase still runs its transactions so another protocol
state can produce useful diagnostic data, and the after-test recovery is always
attempted.

Each `flash test ...` testcase performs a recovery boundary before and after
its protocol-specific transactions. A recovery boundary is best-effort: a
command framed for the wrong current protocol may be accepted by the STM32
controller but ignored by the flash. Only the final SPI JEDEC response can
confirm that recovery reached a usable SPI state.

The test commands use address zero and read at most 16 bytes. The direct
`flash read` and `flash sfdp` commands accept offsets within the configured
4 MiB device and limit one dump to 256 bytes.

## Test details

`flash test spi` runs standard serial-flash transactions through QUADSPI:

- `9F` JEDEC ID, `1-0-1`.
- `05` status register 1, `1-0-1`.
- `5A` SFDP, `1-1-1`, 8 dummy cycles.
- `03` normal read twice, `1-1-1`, to check repeatability.

`flash test qspi-1line` compares normal `03` and fast `0B` reads, both using
`1-1-1` signalling. `flash test qspi` runs this test and the four-line test.

`flash test qspi-4line` reads status first and reports WIP, WEL, and QE. It
then tests:

- `6B` fast quad-output read, `1-1-4`, 8 dummy cycles.
- `EB` fast quad-I/O read, `1-4-4`, an 8-bit mode byte on four lines and four
  additional dummy cycles.

The test never writes the QE bit. Quad-SPI reads may therefore be rejected or
return invalid data when QE is zero.

`flash test qpi` enters QPI with `35` on one instruction line, then tests both
QPI JEDEC forms (`AF` and `9F`) and four-line `0B` and `EB` reads. It exits QPI
with `F5` on four instruction lines before the after-test recovery boundary.

The current implementation deliberately does not issue `WREN`, `WRSR`, erase,
program, or other write commands.

Expected initial observations:

- `flash info` reports 4,194,304 configured bytes.
- SPI JEDEC returns the physical device's three-byte JEDEC ID. The expected ID
  for the documented part is commonly `9d 60 16`; confirm the fitted marking.
- SPI `flash sfdp` starts with the SFDP signature bytes `53 46 44 50` if the
  device responds correctly.
- SFDP density identifies a 32-Mbit device.
- Repeated normal reads return stable data.
- An erased memory array normally reads as `ff`, not `00`.

The diagnostic intentionally tests quad and QPI paths even when the single-line
path fails, so that a valid response from another protocol state can identify
the cause of the original zero response.

## Failure interpretation

- No CDC device: check USB VBUS, D+/D-, the USB-select switch, and the USB
  controller clock before changing QSPI settings.
- CDC works but `flash info` reports the controller is not ready: inspect QSPI pin assignment,
  3V3, `NCS#`, `IO2`, and `IO3` levels during reset and initialization.
- `flash probe` returns a valid QPI ID but SPI is invalid: exit QPI with
  `flash recover`, then repeat the SPI test.
- SPI JEDEC/SFDP fails while the controller is ready: inspect `NCS#`, `CLK`,
  and all six QSPI signal mappings with a logic analyzer.
- JEDEC works but SFDP fails: check the command-state reset, SPI timing, and
  the flash datasheet's SFDP access requirements.
- SPI works but QSPI fails: inspect QE, IO2/WP#, IO3/HOLD#/RESET#, four-line
  pin mapping, mode-byte timing, and signal integrity.
- SPI/QSPI works but QPI fails: inspect the `35` entry transaction, four-line
  instruction nibble order, and `F5` exit transaction.
- All modes return `00`: first verify `NCS#`, `CLK`, flash VCC, reset/HOLD
  levels, and the actual flash part. This is not an erased-array indication.
- All modes return `ff`: distinguish an undriven bus from valid erased-array
  data by checking JEDEC/SFDP and observing `SO/IO1` with a logic analyzer.

Do not connect the target to a host as USB MSC and do not enable FAT or update
storage from this image. BackupApplication in the normal image set exposes the
same unverified external flash through MSC and remains subject to the migration
hardware gates.
