# EF800 Development Guide

This repository ports the EF800 firmware from Mbed OS to Zephyr. Develop and
test inside the reusable devcontainer. Do not install or run `west` directly on
the Windows host.

## Container Lifecycle

The repository is mounted at `/workspace/application`. The west workspace,
Zephyr checkout, modules, Python environment, and toolchain remain inside the
container at `/workspace`, allowing the same container to be reused.

Use the existing container whenever it is available:

```powershell
docker start EF800_Devcontainer
docker exec EF800_Devcontainer /opt/venv/bin/west topdir
```

Create it only when it does not exist:

```powershell
docker run -d --name EF800_Devcontainer `
  --mount "type=bind,source=$($PWD.Path),target=/workspace/application" `
  ghcr.io/jaster-prj/devcontainer-zephyr:latest sleep infinity
```

The equivalent VS Code configuration is `.devcontainer/devcontainer.json`.
Do not remove or recreate the container merely to obtain a clean application
build; use a pristine build instead.

## West Setup

Do not run `west update` as a routine build step. It is slow and downloads a
large dependency set. First inspect the persistent workspace:

```powershell
docker exec EF800_Devcontainer /opt/venv/bin/west topdir
docker exec EF800_Devcontainer /opt/venv/bin/west config manifest.path
docker exec EF800_Devcontainer /opt/venv/bin/west config manifest.file
docker exec EF800_Devcontainer bash -lc "git -C /workspace/zephyr describe --tags --always"
```

The expected values are `/workspace`, `application/manifest-repo`, `west.yml`,
and the Zephyr revision in `manifest-repo/west.yml` (currently `v4.5.0`). If
those values match and the projects are present, build without an update.

Run the following initialization only for a newly created container whose
workspace is missing, after the manifest changed, or after confirming that a
required module is absent:

```sh
if [ -d /workspace/.west ]; then
    /opt/venv/bin/west config manifest.path application/manifest-repo
    /opt/venv/bin/west config manifest.file west.yml
else
    /opt/venv/bin/west init -l application/manifest-repo
fi
/opt/venv/bin/west update
/opt/venv/bin/west zephyr-export
/opt/venv/bin/python -m pip install -r zephyr/scripts/requirements.txt
```

## Builds

Run build commands from `/workspace` through `docker exec`. The build output is
written below the mounted `application/build/` directory and is ignored by
Git.

Configure or pristine-build each firmware target with:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-main -d application/build/ef800-main
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-backup -d application/build/ef800-backup
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-bootloader -d application/build/ef800-bootloader
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard -d application/build/ef800-keyboard
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard-bootloader -d application/build/ef800-keyboard-bootloader
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard-updater -d application/build/ef800-keyboard-updater
```

For normal source-only iterations, reuse the configured build directory:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -d application/build/ef800-main
```

Every build keeps the generated firmware inside its build directory. The
primary binary, Intel HEX file, ELF, map, and generated DeviceTree are located
at:

```text
build/<target>/zephyr/zephyr.bin
build/<target>/zephyr/zephyr.hex
build/<target>/zephyr/zephyr.elf
build/<target>/zephyr/zephyr.map
build/<target>/zephyr/zephyr.dts
```

Use the build-directory `zephyr.bin` directly for programming. Do not copy
firmware to a temporary directory or use an older artifact from `%TEMP%`.

Use a pristine build after changing Kconfig, DeviceTree, overlays, board files,
the manifest, or CMake source lists.

Build directories created before the persistent container may contain absolute
paths from another mount layout. If pristine handling reports a path such as
`/workspace/zephyr/zephyr/cmake/pristine.cmake`, do not repair or reuse that
cache. Build into a new directory or remove only that ignored build directory.

## Flashing

STM32CubeProgrammer is installed on the Windows host at:

```text
C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe
```

Two ST-LINK probes are normally used. Always select the probe explicitly and
verify the detected device before programming:

```text
L431 keyboard probe: 0672FF504955857567234934, device ID 0x435
L496 main/debug probe: 066FFF504982494867074032, device ID 0x461
```

Do not flash an image built for one MCU onto the other. The NUCLEO-L496ZG
ST-LINK Programmer Voltage output is not usable for the EF800 main/debug
target: the EF800 PCB voltage is not reported to the debugger, and the tool
may show an unusably low value such as `0.03V` even when the PCB is powered.
Use the detected device ID/name to identify the MCU, and verify EF800 PCB
power and VTREF at the hardware instead of relying on the Programmer Voltage
field. A Nucleo ST-LINK voltage reading does not replace those checks.

Flash the temporary `test-hwdebug` image at `0x08000000`:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst sn=066FFF504982494867074032 -d "build\test-hwdebug\zephyr\zephyr.bin" 0x08000000 -rst
```

The command must use probe `066FFF504982494867074032` and report `Device ID : 0x461` and `Device name : STM32L496xx`.
The hwdebug image replaces the normal internal-flash image and must be restored
before returning the target to normal EF800 operation.

Flash the keyboard-controller image at `0x08000000` only when the ST-LINK is
connected to the STM32L431:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst sn=0672FF504955857567234934 -d "build\ef800-keyboard\zephyr\zephyr.bin" 0x08000000 -rst
```

The command must report `Device ID : 0x435` and `Device name : STM32L43xxx/STM32L44xxx`.
If the programmer reports `DEV_CONNECT_ERR`, do not retry by flashing another
image. Check target power, SWD wiring, reset state, and ST-LINK target selection
first.

For a controlled two-target build-and-flash pass, use separate commands with
the target-ID checks above. Do not chain flashing commands without checking the
probe result; a successful download to the wrong MCU is still a destructive
mistake. The normal sequence is:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard -d application/build/ef800-keyboard
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/test-hwdebug -d application/build/test-hwdebug
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst sn=0672FF504955857567234934 -d "build\ef800-keyboard\zephyr\zephyr.bin" 0x08000000 -rst
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst sn=066FFF504982494867074032 -d "build\test-hwdebug\zephyr\zephyr.bin" 0x08000000 -rst
```

For the keyboard link, the physical L496 pin functions are `PG7=LPUART1_TX`
and `PC0=LPUART1_RX`. The STM32 HAL diagnostic enables the internal UART swap
(`UART_ADVFEATURE_SWAP_ENABLE`) to match the fitted board wiring. Verify the
hwdebug console with `lpuart test`; successful output includes `byte=0xaa` and
`PASS`.

The normal keyboard communication default is `115200 8N1` on both controllers.
LPUART1 uses TX/RX swap; UART4, which is the physical normal-image debug
console through the CP2102 bridge on `COM7`, also uses `115200 8N1`. The
hwdebug USB CDC console appears separately as `COM10`. Do not change the keyboard link to 9600 while debugging the
normal protocol unless both images are rebuilt and flashed together.

The optional selector protocol is separate from the normal default:

```text
E3 01 -> request 9600
E3 05 -> request 115200
E4    -> request default 115200
```

The keyboard ACKs the selector at the old baud, waits before reconfiguring, and
sends `0xaa` at the new baud. The L496 must receive the complete `a0 e3 7f`
frame before changing its baud. The baud-transition test is not a substitute
for the normal protocol test.

## Tests

The hwdebug USB CDC console is on `COM10`; the normal-image UART4 console is
on the CP2102 bridge at `COM7`. After flashing the matching images, run these
commands in order:

```text
lpuart test
keyboard version-raw
keyboard id-raw
keyboard test
```

Expected baseline results are `lpuart test ... PASS`, raw version bytes
`a0 f1 01 01 01`, raw ID bytes `a0 f2 ab 83`, and `keyboard/protocol PASS`.
The `keyboard baud-test` command is optional and should be run separately;
first establish that the normal `115200` protocol passes.

Keep the UART/CDC console process closed between scripted `COM7`/`COM10` accesses. A locked
COM port produces no useful firmware result and can be mistaken for a target
failure.

The keyboard ID and version raw commands are the preferred diagnostics when a
high-level test reports a missing byte. They distinguish a keyboard firmware
response problem from an L496 receive-path problem. Do not weaken expected
protocol frames to accept truncated responses.

Build and run the native keyboard, protocol, RTC, and persistence tests with:

```powershell
docker exec -e ZEPHYR_BASE=/workspace/zephyr -w /workspace EF800_Devcontainer /opt/venv/bin/west twister -T application/tests -p native_sim --inline-logs --no-clean
```

All six firmware builds and the native tests must pass before considering a
porting increment complete. The current closeout baseline is 31 native test
cases. Keep `docs/migration-status.md` authoritative when the suite changes.

## Image Contract

The fixed internal-flash layout is part of the compatibility contract:

```text
bootloader  0x08000000  0x00010000
backup      0x08010000  0x00018000
metadata    0x08028000  0x00008000
main        0x08030000  0x000D0000
```

After a pristine build, verify both binary sizes and linked addresses:

```powershell
docker exec -w /workspace EF800_Devcontainer bash application/scripts/verify-images.sh
```

Pass six alternate build directories when validating nonstandard local build
names. Their order is bootloader, backup, main, keyboard, keyboard bootloader,
and keyboard updater.

Never flash a MainApplication image linked at `0x08000000`; it would overlap
the second-level bootloader.

## Hardware Gates

Keep MainApplication FAT mounting, bootloader update storage, and USB sharing
release-gated until the fitted QSPI part, capacity, erase geometry, maximum
frequency, and existing FAT volume are fully verified. The current recoverable
bring-up target has mounted FAT successfully, but this is not production
qualification. Never allow MainApplication to mount the QSPI FAT volume while
BackupApplication exposes the same disk through USB MSC.

Hardware changes must also validate the MCP23017 wiring, USB-select polarity,
keyboard reset and UART protocol, T6963 timing and polarity, board oscillator,
DS3231 behavior, and MAX17043 readings.

## Source of Truth

Use `mbedos/EF800` only as the legacy behavior reference. New implementation
belongs under this repository's `application/`, `shared/`, `boards/`, and
`tests/` trees. Keep `docs/migration-status.md` and relevant status documents
current when an increment is completed or a hardware assumption changes.
