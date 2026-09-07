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

All three protocol families use the STM32L496 `QUADSPI` peripheral. The existing
flash is on Bank 1 and the newly fitted W25Q128JVSQ is on Bank 2. Both banks
share `CLK=PF10`, but have separate chip-select and IO pin groups.
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

## Flash banks

Bank 2 is the Winbond `W25Q128JVSQ`, 128 Mbit / 16 MiB:

| Signal | STM32 pin |
|---|---|
| IO0 | PC1 |
| IO1 | PC2 |
| IO2/WP# | PC3 |
| IO3/HOLD#/RESET# | PC4 |
| CLK | PF10 |
| NCS# | PD3 |

The debug image selects the bank when initializing the STM32 QUADSPI HAL. The
two flashes are independent devices; dual-flash mode is disabled.

## Console commands

Offsets are zero-based offsets within the selected external flash. Existing
commands operate on Bank 1. `flash info 2` selects and queries Bank 2. The
dedicated `flash2` commands provide read-only Bank 2 JEDEC/status and SFDP
diagnostics. All commands are
read-only and limit one dump to 256 bytes.

```text
flash info [1|2]
flash2 recover
flash2 probe
flash2 sfdp [offset] [length]
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

- `flash info` reports 4,194,304 configured bytes for Bank 1.
- `flash info 2` reports 16,777,216 configured bytes and reads the Bank 2 JEDEC ID.
- SPI JEDEC returns the physical device's three-byte JEDEC ID. The expected ID
  for the documented part is commonly `9d 60 16`; confirm the fitted marking.
- Bank 2's expected W25Q128JV JEDEC ID is commonly `ef 40 18`; confirm the
  fitted marking and voltage variant.
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

## Latest bench result

On 2026-09-03 the recoverable L496 target was connected through ST-LINK
`0672FF504955857567234934` at 3.25 V. The target was identified as device ID
`0x461`, revision 2.0, using Under Reset SWD access. The cleaned debug image
was programmed at `0x08000000` after the external flash was renewed; the
59,608-byte download verified successfully. The debug image enumerated as CDC
`COM6` with USB PID `0xEF81`, and the watchdog reported as feeding.

The following read-only tests completed at the STM32 HAL level but returned
all-zero data:

| Test | Observation |
|---|---|
| SPI JEDEC `9F` | `00 00 00` |
| SPI status `05` | `00` |
| SPI SFDP `5A` | all `00`, invalid signature |
| SPI normal/fast read | all `00`, repeated match |
| QSPI `1-1-4` / `1-4-4` | all `00` |
| QPI JEDEC `AF` / `9F` | all `00` |
| QPI `0B` / `EB` reads | all `00` |

Recovery commands also completed at the QUADSPI controller but could not be
confirmed by a plausible SPI JEDEC response. No valid flash identity or SFDP
signature was obtained. The same all-zero result was reproduced after the
external flash renewal. The target remains in debug-image mode with FAT and
USB MSC disabled and is currently held halted through Under Reset SWD access.

An SWD register read while the target was idle showed `PE11` (`NCS#`) high and
`PF6` (`IO3`) high. `PF7` (`IO2/WP#`) read low despite its configured internal
pull-up; `PF8` and `PF9` also read low. The `PF7` level is a priority hardware
check, but static readings alone do not prove that a transaction was active.

The cleaned-image run produced no verbose Zephyr log flood. `flash info`
reported QUADSPI ready at 1 MHz, while `flash probe`, `flash recover`, and
`flash test all` still returned all-zero receive data. The final SWD reconnect
reported the core halted, so the target was left safely halted rather than
running an unverified image.

Next hardware actions:

1. Measure external-flash VCC directly at the flash package against GND.
2. Measure `NCS#`, `IO2/WP#`, and `IO3/HOLD#/RESET#` while the MCU is in reset
   and while the debug image is idle. Confirm that `IO2` and `IO3` are not
   externally pulled low.
3. Use a logic analyzer on `NCS#`, `CLK`, `IO0`, and `IO1` during
   `flash test spi`. Confirm that `9F` is present on `IO0` and that clock and
   chip-select activity reach the flash package.
4. Check the actual fitted flash marking and schematic connection against the
   documented `IS25LP032D-JTLE-TR` pin assignment.
5. Repeat `flash probe` and `flash recover` after the electrical checks. Do not
   enable FAT, USB MSC, erase, program, or QE writes until a valid JEDEC or
   SFDP response is obtained.

## Bank 2 bench result

## Clock source correction

The external 25 MHz HSE oscillator was removed from the target. The board
DeviceTree now disables HSE and uses the STM32L496 internal 16 MHz HSI as the
PLL source with `M=1`, `N=10`, and `P=2`, preserving an 80 MHz system clock.
The HSI48 clock remains enabled for USB.

The HSI image was built and flashed on 2026-09-05. SWD core-register capture
showed the CPU in Zephyr's `arch_cpu_idle()` WFI loop after startup, rather than
in the previous HSE-ready wait loop. This proves the image reaches `main()` and
is waiting for USB CDC DTR. The target still did not enumerate a CDC device,
so the remaining issue is the physical USB path or USB-select hardware, not
the removed HSE oscillator.

On 2026-09-04 the powered target was programmed with the verified Bank 2
debug-image update at `0x08000000`. STM32CubeProgrammer verified the 60,048-byte
download successfully. The target enumerated the CDC console as `COM6`; `COM5`
remained the ST-LINK virtual COM port.

The following read-only commands selected both banks successfully:

```text
flash info
flash info 2
flash info 1
```

Bank 1 reported the expected controller and pin configuration:

```text
controller-ready: yes
size: 4194304 bytes (32 Mbit)
controller: STM32 QUADSPI Bank 1 at 1000000 Hz
pins: IO0=PF8 IO1=PF9 IO2=PF7 IO3=PF6 CLK=PF10 NCS=PE11
data: 00 00 00
```

Bank 2 reported the expected controller and newly fitted pin configuration:

```text
controller-ready: yes
size: 16777216 bytes (128 Mbit)
controller: STM32 QUADSPI Bank 2 at 1000000 Hz
pins: IO0=PC1 IO1=PC2 IO2=PC3 IO3=PC4 CLK=PF10 NCS=PD3
data: 00 00 00
```

Switching back to Bank 1 after Bank 2 also succeeded. Both Bank 1 and Bank 2
JEDEC transactions completed at the HAL level but returned `00 00 00`, so the
W25Q128JVSQ has not yet been electrically identified. Check Bank 2 VCC,
`NCS#`, `IO2/WP#`, `IO3/HOLD#/RESET#`, and the PC1-PC4/PD3 wiring with a logic
analyzer before enabling further protocol tests.

The explicit Bank 2 diagnostics produced the same result:

```text
flash2 probe
JEDEC data: 00 00 00
status data: 00

flash2 sfdp 0 64
SFDP signature: invalid
data: all 00
```

This rules out a Bank 2-only command-path omission in the debug image. Both
banks are selected and initialized, but neither flash drives a nonzero response
on the single-line read transactions. The target remains in the read-only
debug-image state; no erase, program, QE, FAT, or MSC operation was performed.

`flash2 recover` sends only the Winbond-compatible `AB`, `66`, and `99` SPI
commands, followed by a JEDEC read. These commands do not erase, program, or
change the Quad Enable bit. Use it once after power-up if the part may have
been left in deep power-down or an unknown reset state. If it still returns
all zeroes, stop firmware experimentation and inspect the electrical signals.

On 2026-09-04, `flash2 recover` completed all three command transactions with
no HAL error, but the follow-up JEDEC response remained `00 00 00`. A subsequent
`flash2 probe` and `flash2 sfdp 0 64` also returned all zeroes. The recovery
sequence therefore did not reveal a device in deep power-down or a recoverable
SPI protocol state.
