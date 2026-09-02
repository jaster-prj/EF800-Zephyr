# STM32L496 target snapshot

This document records the STM32L496 engineering target connected through the
ST-LINK probe on 2026-09-02. The target was deliberately erased after the
legacy firmware captures had been checked. It is currently blank and no
firmware was restored.

## Connection

STM32CubeProgrammer version `2.23.0` was used from the Windows host. The
connected probe reported:

| Item | Value |
|---|---|
| ST-LINK serial | `0672FF504955857567234934` |
| ST-LINK firmware | `V2J47M34` |
| Probe board | `NUCLEO-F767ZI` |
| Target interface | SWD |
| Target voltage | `3.24-3.25 V` |
| Target device | `STM32L496xx/STM32L4A6xx` |
| Device ID | `0x461` |
| Revision | `Rev 2.0` |
| Internal flash | `1 MiB` |

The probe board name identifies the ST-LINK hardware, not the target MCU. The
target was connected first with HotPlug and then with Under Reset. The Under
Reset result is authoritative for this snapshot because the HotPlug session
reported a CubeProgrammer database warning and stale-looking option-byte data.

## Recovery captures

The following files were checked before erasing the target. They are fixed-size
captures of the legacy image regions, not a complete 1 MiB flash dump.

| File | Original address | Size | Initial stack pointer | Reset vector | SHA-256 |
|---|---:|---:|---:|---:|---|
| `Backup/Bootloader.bin` | `0x08000000` | `0x10000` | `0x2004FF00` | `0x0800BABD` | `9744963130DA78E17D46A653240EFC5BEDF6BE256BDD31A91D042B99F7AAF122` |
| `Backup/BackupApplication.bin` | `0x08010000` | `0x20000` | `0x2004FF00` | `0x08024AE5` | `9A631AB596AD9CB8960F0795826681CD1CD95E4731D0409508BCBB48E7495D52` |
| `Backup/MainApplication.bin` | `0x08030000` | `0x80000` | `0x2004FF00` | `0x08046FA9` | `DD8957F2FC685F07C464009BD1B0B091AFF9DBDF3B27B23F4C2EAB701E407FA1` |

The captures cover `0x08000000..0x080AFFFF`. The final
`0x080B0000..0x080FFFFF` range was not captured because the target was still
read-protected during the original download. Erasing that uncaptured range was
explicitly approved for this disposable development target.

The legacy backup capture is `128 KiB`, and its reset vector is in the upper
half at `0x08024AE5`. Therefore, the legacy backup image uses the address range
that the current Zephyr layout assigns to the metadata partition. See
`docs/flash-layout.md` for the distinction between the legacy capture and the
current Zephyr contract.

## Option bytes before erase

The first HotPlug `-ob displ` command reported `RDP=0xB7` (Level 1), together
with a warning that all five STM32L496 database configurations were disabled.
Direct reads of the option-byte and mirror addresses repeated `0x08041BB7` or
debug-register-looking values, so the PCROP and WRP fields from that session
are not retained as a valid pre-erase snapshot.

An Under Reset `--readunprotect` invocation subsequently reported `RDP=0xAA`
and said that the option byte was unchanged, while the legacy bootloader was
still present. This means the HotPlug `0xB7` result was not reliable evidence of
the actual option-byte state. No option-byte value from the HotPlug session was
used to configure the target.

## Erase operation

The following read-only and destructive operations were performed in order:

```text
STM32_Programmer_CLI.exe -c port=SWD mode=HotPlug sn=0672FF504955857567234934 -ob displ
STM32_Programmer_CLI.exe -c port=SWD mode=UR reset=HWrst sn=0672FF504955857567234934 -rdu
STM32_Programmer_CLI.exe -c port=SWD mode=UR reset=HWrst sn=0672FF504955857567234934 -e all
```

The first `-rdu` command did not provide reliable erase evidence. The explicit
Under Reset `-e all` command then reported `Mass erase successfully achieved`.
No firmware or option-byte programming command was run afterward.

## Current option bytes

The following values were read with Under Reset after the mass erase:

```text
FLASH_OPTR   0x40022020: 0xFBEEF8AA
FLASH_PCROP1SR 0x40022024: 0xFFFFFFFF
FLASH_PCROP1ER 0x40022028: 0xFFFF0000
FLASH_WRP1AR   0x4002202C: 0xFF00FFFF
FLASH_WRP1BR   0x40022030: 0xFF00FFFF
FLASH_PCROP2SR 0x40022044: 0xFFFFFFFF
FLASH_PCROP2ER 0x40022048: 0xFFFF0000
FLASH_WRP2AR   0x4002204C: 0xFF00FFFF
FLASH_WRP2BR   0x40022050: 0xFF00FFFF
```

The CubeProgrammer decoded fields are:

| Field | Value | Meaning |
|---|---:|---|
| RDP | `0xAA` | Level 0, no readout protection |
| BOR_LEV | `0x0` | BOR Level 0, threshold around 1.7 V |
| nRST_STOP | `0x1` | No reset entering Stop mode |
| nRST_STDBY | `0x1` | No reset entering Standby mode |
| nRST_SHDW | `0x1` | No reset entering Shutdown mode |
| IWDG_SW | `0x0` | Hardware independent watchdog selection |
| IWDG_STOP | `0x1` | IWDG remains active in Stop mode |
| IWDG_STDBY | `0x1` | IWDG remains active in Standby mode |
| WWDG_SW | `0x1` | Software window watchdog selection |
| BFB2 | `0x0` | Dual-bank boot disabled |
| DBANK | `0x1` | Raw option bit selects dual-bank flash organization |
| nBOOT1 | `0x1` | BOOT0=1 selects system memory |
| SRAM2_PE | `0x1` | SRAM2 parity check disabled |
| SRAM2_RST | `0x1` | SRAM2 is not erased on system reset |
| nSWBOOT0 | `0x0` | BOOT0 is taken from the option bit nBOOT0 |
| nBOOT0 | `0x1` | BOOT0=0 selects main flash memory |
| PCROP1_STRT / END | `0xFFFF / 0x0` | No active Bank 1 PCROP range displayed |
| PCROP_RDP | `0x1` | PCROP zone erased when RDP is decreased |
| WRP1A/B_STRT / END | `0xFF / 0x0` | No active Bank 1 WRP range displayed |
| PCROP2_STRT / END | `0xFFFF / 0x0` | No active Bank 2 PCROP range displayed |
| WRP2A/B_STRT / END | `0xFF / 0x0` | No active Bank 2 WRP range displayed |

The raw `FLASH_OPTR` value is the authoritative record for fields not labelled
by the CubeProgrammer output. No option-byte programming was performed after
the erase. In particular, the observed state is mixed: IWDG is selected in
hardware mode, while WWDG is selected in software mode. Any future Zephyr
image flashed with these settings must account for the IWDG starting in
hardware mode and remaining active in low-power modes.

## Erase verification

CubeProgrammer's full blank check completed successfully:

```text
Flash memory is empty.
```

Sequential reads from both internal flash bank starts also returned all
`0xFFFFFFFF`:

```text
0x08000000: FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF ...
0x08080000: FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF ...
```

The target is intentionally left blank. Do not flash the current bootloader,
backup, main, or hardware-debug images until their watchdog behavior and the
remaining hardware gates have been validated.
