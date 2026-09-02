# EF800 QSPI hardware debug image

`ef800-hwdebug` is a temporary, read-only hardware bring-up image. It replaces
the second-level bootloader at `0x08000000` on one recoverable engineering
target. It is not part of the normal four-image boot chain and must be replaced
before returning the target to normal firmware.

## Scope

The image provides:

- USB CDC-ACM as a small line-oriented diagnostic command console.
- Read-only QSPI JEDEC-ID, SFDP, geometry, and raw-memory commands.
- Single-line SPI commands during the first flash probe.
- Internal STM32 pull-ups on `NCS#`, `IO2/WP#`, and `IO3/RESET#/HOLD#`.

The PCB currently has no external pull-ups. For production stability, add
approximately 4.7 kOhm to 10 kOhm from `NCS#`, `IO2/WP#`, and
`IO3/HOLD#/RESET#` to 3V3, placed close to the flash. Do not add pull-downs to
these lines. The internal pull-ups are inactive while the STM32 is in reset.

The image does not provide:

- FAT or filesystem access.
- USB MSC.
- Bootloader update installation.
- Flash erase, program, or write shell commands.
- Automatic Quad Enable register programming.

The QSPI description is for the fitted `IS25LP032D-JTLE-TR`: 32 Mbit,
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

The checker requires `rom_start=08000000` and an image no larger than 64 KiB.

## Flash and recovery

Before flashing:

1. Use only a sacrificial or fully recoverable target with SWD access.
2. Save the existing internal flash, option bytes, and QSPI contents if
   possible.
3. Disconnect the USB host during programming.
4. Flash only the debug image at `0x08000000`.

The debug image overwrites the normal second-level bootloader. Restore the
normal boot chain before using the device as an EF800 system again.

## USB console

Connect the USB host after programming and power-cycle the target. The device
enumerates as `Jaster EF800 HW Debug` using temporary engineering USB
identifiers. Open the assigned CDC serial port and assert DTR. The diagnostic
command console is available after DTR is asserted.

If the board contains a USB-select switch controlled by MCP23017 pin 8, CDC
enumeration also requires that switch to select the STM32 USB path. The
standalone image does not change MCP23017 outputs because the switch polarity
is not yet confirmed. Verify the USB path and switch state from the schematic
before treating a missing CDC device as a firmware failure.

## QSPI commands

Offsets are zero-based offsets within the external 4 MiB flash. All commands
are read-only and limit one dump to 256 bytes.

```text
qspi info
qspi jedec
qspi sfdp
qspi sfdp 0 256
qspi read 0 64
qspi read 0x1000 128
```

Expected initial observations:

- `qspi info` reports 4,194,304 configured bytes.
- `qspi jedec` returns the physical device's three-byte JEDEC ID.
- `qspi sfdp` starts with the SFDP signature bytes `53 46 44 50` if the
  device responds correctly.
- SFDP density identifies a 32-Mbit device.
- `qspi read` returns stable data at repeated reads of the same address.

The first image uses single-line SPI commands and does not test quad data
transfers. A separate quad-mode test should only be added after JEDEC/SFDP
reads are stable and the Quad Enable behavior is confirmed from the device
documentation.

## Failure interpretation

- No CDC device: check USB VBUS, D+/D-, the USB-select switch, and the USB
  controller clock before changing QSPI settings.
- CDC works but `qspi info` reports not ready: inspect QSPI pin assignment,
  3V3, `NCS#`, `IO2`, and `IO3` levels during reset and initialization.
- JEDEC/SFDP fails while the driver is ready: inspect `NCS#`, `CLK`, and the
  six QSPI signal mappings with a logic analyzer.
- JEDEC works but SFDP fails: check the command-state reset, SPI timing, and
  the flash datasheet's SFDP access requirements.
- JEDEC/SFDP works but quad mode later fails: check Quad Enable status-register
  behavior and signal integrity separately; do not enable FAT yet.

Do not connect the target to a host as USB MSC and do not enable FAT or update
storage from this image. BackupApplication in the normal image set exposes the
same unverified QSPI device through MSC and remains subject to the migration
hardware gates.
