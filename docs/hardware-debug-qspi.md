# EF800 external flash hardware debug image

## Watchdog reset diagnostic

The hardware-debug console reports the reset reason at every boot. Run:

```text
watchdog test
```

The command stops feeding the IWDG. After approximately one second, the MCU
resets. On the next console startup, the header must include:

```text
boot: reason=watchdog
```

This confirms both that the IWDG reset occurred and that the reset cause was
read from the STM32 reset-status registers. A normal startup reports
`boot: reason=normal`.

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
- Peripheral bring-up commands for keyboard-controller protocol, T6963
  read/write, selectable UART3/UART1 loopback, and manual FAT/MSC ownership
  confirmation. The `storage test` command enables FAT and USB MSC only for
  its controlled, interactive ownership sequence.

The PCB currently has no external pull-ups. For production stability, add
approximately 4.7 kOhm to 10 kOhm from `NCS#`, `IO2/WP#`, and
`IO3/HOLD#/RESET#` to 3V3, placed close to the flash. Do not add pull-downs to
these lines. The internal pull-ups are inactive while the STM32 is in reset.

All three protocol families use the STM32L496 `QUADSPI` peripheral. This
debug image deliberately scopes testing to the fitted Bank 1 flash. Bank 2 is
not instantiated, selected, or tested by this image.
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

The hwdebug USB CDC console is currently assigned `COM10`. The CP2102
physical-UART bridge on `COM7` is UART4 and is used by the normal
Bootloader/BackupApplication/MainApplication images, not by hwdebug's command
console.

The external flash may remain powered throughout MCU programming and reset.
Do not assume that an MCU reset also reset the flash. Start with `flash probe`
to observe the current state without sending reset or mode-changing commands.

If the board contains a USB-select switch controlled by MCP23017 pin 8, CDC
enumeration also requires that switch to select the STM32 USB path. The
standalone image does not change MCP23017 outputs because the switch polarity
is not yet confirmed. Verify the USB path and switch state from the schematic
before treating a missing CDC device as a firmware failure.

## Console commands

Offsets are zero-based offsets within the Bank 1 external flash. All commands
are read-only and limit one dump to 256 bytes.

```text
hardware test flash
hardware test mcp23017
hardware test all
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
qspi bitbang spi
qspi bitbang qpi
keyboard test
keyboard interactive
display test
display interactive
uart test port1|port2
storage test
```

`keyboard test` uses the L496-to-L431 keyboard link at `115200 8N1` and runs
echo, ID, activate, typematic, debug enable, virtual `a` make/break, debug
disable, and deactivate checks. It then prints instructions for the separate
physical keyboard test. Start it separately with `keyboard interactive`; confirm
it with `pass` or `fail`, or leave it with `cancel` or `exit`.

`display test` initializes the T6963, writes `EF800 LCD TEST`, reads the text
memory back, and compares the bytes. Start the visible display confirmation
separately with `display interactive`; use `pass` or `fail`, or leave it with
`cancel` or `exit`.

`uart test port1` selects USART3 and `uart test port2` selects USART1. Both
use `115200 8O2` and expect a local TX-to-RX loopback through the approved
RS232 level converter. Only one selected port is tested at a time.

The debug image provides a destructive, interactive FAT/MSC ownership test.
It uses the QSPI partition directly and keeps exactly one owner active at a
time. Existing data is deleted only after the explicit `format-confirm`
response.

Run:

```text
storage test
format-confirm
```

The test formats the volume and creates:

```text
/storage_test/created_by_fat.txt  (created by internal FAT)
/storage_test/dummy.txt           (EF800 storage test dummy)
```

It reads both files through the internal FAT API and reports
`storage/internal-fat PASS`. FAT is then unmounted, the disk is synchronized,
and USB MSC is enabled. Connect the USB host and reply `pass` only after both
files are visible and readable, or reply `fail` if enumeration or reading
fails.

After the MSC read test, create this file from the host:

```text
/storage_test/created_by_msc.txt
```

with the content `created by USB MSC`, safely eject the volume, and reply
`pass`. The test disables MSC, remounts FAT, and verifies that the new file is
readable through the internal filesystem. It then performs a final FAT
unmount/remount and verifies all three files. The final result is
`STORAGE TEST PASS` or `STORAGE TEST FAIL`.

The hwdebug MSC test does not drive the MCP23017 USB-select output. On the
tested board this switch is bypassed by solder bridges, so the STM32 USB path
is permanently selected.

Use `cancel` or `exit` to leave the test. Do not connect the USB host before
the console asks for it, and always safely eject the MSC volume before replying
to the MSC-created-file step. The storage test is destructive and is intended
only for a recoverable engineering target.

The `qspi` command name remains accepted as an alias for `flash`.

Grouped test output is intentionally compact:

```text
RUN   flash/spi
ERROR SPI JEDEC or read validation failed (error=-5)
FAIL
RUN   mcp23017/probe
PASS
```

Transaction traces and data dumps are suppressed during grouped tests. An
`ERROR` line is emitted immediately before `FAIL`. The flash sequence performs
recovery at its boundaries and uses explicit SPI, QSPI, QPI, and bit-banged
protocol detection tests; it does not claim to read a direct flash mode
register.

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

## Historical bench result

This section records the early 2026-09-03 hwdebug investigation. It is retained
as forensic evidence and is not the current target state. Current migration
status is maintained in `docs/migration-status.md`.

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

The current debug image is Bank 1-only. Historical Bank 2 bring-up results are
intentionally not part of this command or output contract.
