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
and the Zephyr revision in `manifest-repo/west.yml` (currently `v4.4.1`). If
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

The reusable ST-LINK has serial number `0672FF504955857567234934`. Always
verify the detected device before programming. The EF800 main/debug target is
the STM32L496 with device ID `0x461`; the keyboard controller is an STM32L431
with device ID `0x435`. Do not flash an image built for one MCU onto the other.

Flash the temporary `ef800-hwdebug` image at `0x08000000`:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst sn=0672FF504955857567234934 -d "build\ef800-hwdebug\zephyr\zephyr.bin" 0x08000000 -rst
```

The command must report `Device ID : 0x461` and `Device name : STM32L496xx`.
The hwdebug image replaces the normal internal-flash image and must be restored
before returning the target to normal EF800 operation.

Flash the keyboard-controller image at `0x08000000` only when the ST-LINK is
connected to the STM32L431:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst -d "build\ef800-keyboard\zephyr\zephyr.bin" 0x08000000 -rst
```

The command must report `Device ID : 0x435` and `Device name : STM32L43xxx/STM32L44xxx`.
If the programmer reports `DEV_CONNECT_ERR`, do not retry by flashing another
image. Check target power, SWD wiring, reset state, and ST-LINK target selection
first.

For the keyboard link, the physical L496 pin functions are `PG7=LPUART1_TX`
and `PC0=LPUART1_RX`. The STM32 HAL diagnostic enables the internal UART swap
(`UART_ADVFEATURE_SWAP_ENABLE`) to match the fitted board wiring. Verify the
hwdebug console with `lpuart test`; successful output includes `byte=0xaa` and
`PASS`.

## Tests

Build and run the native keyboard, protocol, RTC, and persistence tests with:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/tests/keyboard_protocol -d application/build/keyboard-protocol
docker exec -w /workspace EF800_Devcontainer application/build/keyboard-protocol/zephyr/zephyr.exe
```

All four firmware builds and the native tests must pass before considering a
porting increment complete. The current closeout baseline is 24 native tests.

## Image Contract

The fixed internal-flash layout is part of the compatibility contract:

```text
bootloader  0x08000000  0x00010000
backup      0x08010000  0x00010000
metadata    0x08020000  0x00010000
main        0x08030000  0x000D0000
```

After a pristine build, verify both binary sizes and linked addresses:

```powershell
docker exec -w /workspace EF800_Devcontainer bash application/scripts/verify-images.sh
```

Pass four alternate build directories when validating nonstandard local build
names. Their order is bootloader, backup, main, and keyboard.

Never flash a MainApplication image linked at `0x08000000`; it would overlap
the second-level bootloader.

## Hardware Gates

Keep MainApplication FAT mounting and bootloader update storage disabled until
the fitted QSPI part, capacity, erase geometry, maximum frequency, and existing
FAT volume are verified. Do not allow the MainApplication to mount the QSPI FAT
volume while the backup application exposes the same disk through USB MSC.

Hardware changes must also validate the MCP23017 wiring, USB-select polarity,
keyboard reset and UART protocol, T6963 timing and polarity, board oscillator,
DS3231 behavior, and MAX17043 readings.

## Source of Truth

Use `mbedos/EF800` only as the legacy behavior reference. New implementation
belongs under this repository's `application/`, `shared/`, `boards/`, and
`tests/` trees. Keep `docs/migration-status.md` and relevant status documents
current when an increment is completed or a hardware assumption changes.
