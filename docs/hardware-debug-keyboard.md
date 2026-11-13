# Hardware debug keyboard

This guide describes how to test EF800Application workflows on physical EF800
hardware without pressing the fitted keyboard. The test host connects to the
EF800Application console and sends semantic key names. EF800Application sends
those names to the keyboard controller, which resolves each name to its matrix
key and current scancode. The controller then emits the same PS/2-like
scancode bytes used for physical keyboard input.

The debug interface does not bypass EF800Application keyboard processing:

```text
test host console
    -> EF800Application shell
    -> EF800Application keyboard UART
    -> keyboard controller name/matrix/scancode mapping
    -> normal keyboard UART scancode output
    -> EF800Application keyboard parser and application handlers
```

The bootloader and BackupApplication do not consume keyboard-controller UART
input. They need no debug-keyboard command handling.

## Connections

Connect the test terminal to the normal EF800 development console:

```text
UART4 through the CP2102 bridge on `COM7`, 115200 baud, 8 data bits, no parity,
1 stop bit (115200 8N1). The hwdebug image's USB CDC console is a separate
port, currently `COM10`.

Bootloader, BackupApplication, and EF800Application emit the Zephyr startup
banner and route logs to the same UART4 connection.
```

Do not connect the terminal to the binary keyboard-controller link. The
controller link is:

```text
EF800Application LPUART1 to keyboard-controller USART3
115200 baud, 8 data bits, no parity, 1 stop bit (115200 8N1)
```

Use a common ground and the board's approved debug/programming connection.
The exact target wiring, reset behavior, oscillator, MCP23017 wiring, and UART
pin routing must be confirmed against the hardware before treating a failure
as a firmware failure.

## Firmware prerequisites

Build the matching images in the reusable development container. Do not run
`west` directly on the Windows host:

```powershell
docker start EF800_Devcontainer
docker exec EF800_Devcontainer /opt/venv/bin/west topdir
```

Build all four images from `/workspace`:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-application -d application/build/ef800-application
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-backup -d application/build/ef800-backup
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-bootloader -d application/build/ef800-bootloader
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800_keyboard application/application/keyboard-application -d application/build/keyboard-application
```

Verify image sizes and addresses:

```powershell
docker exec -w /workspace EF800_Devcontainer bash application/scripts/verify-images.sh
```

Flash the matching images at the fixed addresses:

| Image | Target | Address |
|---|---|---:|
| Second-level bootloader | L496 | `0x08000000` |
| BackupApplication | L496 | `0x08010000` |
| EF800Application | L496 | `0x08030000` |
| Keyboard Controller | L431 | controller application flash |

Never flash a EF800Application image linked at `0x08000000`; it overlaps the
second-level bootloader. Keep EF800Application FAT mounting and bootloader
update storage disabled until the QSPI hardware gates are verified.

## Console preparation

Open a terminal on the normal EF800Application UART4/CP2102 console (`COM7`) at
`115200 8N1` and capture the complete session log. Disable terminal-local echo
if commands appear twice. USART1 is a product port and is not the fixed shell
console in the current board configuration.

Reset the target and wait for the normal boot output and the keyboard startup
completion message. The debug commands should not be used before the keyboard
controller has started and EF800Application has initialized its keyboard UART.

## Key names

The shell accepts names rather than matrix positions or scancodes. Matching is
ASCII case-insensitive:

```text
keyboard debug press a
keyboard debug press A
keyboard debug press SHIFT
```

The character names are:

```text
a b c d e f g h i j k l m n o p q r s t u v w x y z
0 1 2 3 4 5 6 7 8 9
```

The named non-character keys are:

```text
comma period minus plus less caret
space tab enter escape backspace delete insert
shift ctrl alt caps on off up down left right
```

Aliases are:

```text
return  -> enter
esc     -> escape
back    -> backspace
control -> ctrl
```

Use names such as `space`, `comma`, and `plus` instead of relying on shell
quoting for punctuation.

## Basic test

Start a virtual keyboard session, press and release a key, and stop the
session:

```text
keyboard debug start
keyboard debug press a
keyboard debug release a
keyboard debug stop
```

Expected behavior:

- The application reacts as though the physical `A` key was pressed.
- The key remains held between the press and release commands.
- The controller emits no debug-command acknowledgement.
- Physical matrix scanning is disabled during the session.
- Physical scanning resumes after `keyboard debug stop`.

## Held modifier test

Pressing a modifier is persistent. It must be released explicitly when testing
individual key sequences:

```text
keyboard debug start
keyboard debug press shift
keyboard debug press A
keyboard debug release a
keyboard debug release shift
keyboard debug stop
```

EF800Application should process the `A` make while Shift is held and therefore
receive uppercase `A`. Releasing `a` must not release Shift.

A Ctrl sequence can exercise the existing Ctrl+V application event:

```text
keyboard debug start
keyboard debug press ctrl
keyboard debug press v
keyboard debug release v
keyboard debug release ctrl
keyboard debug stop
```

## Automatic release on stop

The controller releases all virtual keys when debug mode is stopped. This is a
safe cleanup mechanism for interrupted scripts:

```text
keyboard debug start
keyboard debug press shift
keyboard debug press a
keyboard debug stop
```

Before the existing debug-disable response completes, the controller emits a
normal break sequence for `a` and `shift`, clears virtual typematic state, and
then finishes the existing debug-mode command. EF800Application must no longer
have either key held after the command. EF800Application then restores normal
keyboard scanning.

The stop command does not require the test host to know the scancodes. The
controller owns the name, matrix, and scancode mappings.

## Application workflow test

The RTC editor is opened with `z`. Once open, enter values using explicit
press/release pairs. The weekday is displayed as a three-letter name; use the
Up/Down keys to select it and Enter to continue. The remaining fields are:

```text
date, month, year, hour, minute, second, UTC offset, DST
```

For example, the weekday can be advanced once with:

```text
keyboard debug start
keyboard debug press z
keyboard debug release z
keyboard debug press down
keyboard debug release down
keyboard debug press enter
keyboard debug release enter
keyboard debug stop
```

Use the complete RTC workflow only after the basic and held-modifier tests
pass. Storage remains subject to the QSPI/FAT hardware gates.

## Unknown names and safety

Unknown names are intentionally silent. The controller consumes the complete
request but sends neither a scancode nor an error response:

```text
keyboard debug start
keyboard debug press unknown_key
keyboard debug release unknown_key
keyboard debug stop
```

No application key, event, UART response, or echoed request should result from
the unknown name.

Do not include `off` in the default smoke test. The `off` key invokes
EF800Application's real power-down output and should be tested only when that
hardware behavior is understood and monitored.

## Physical keyboard isolation

While a debug session is active, physical matrix scanning is disabled:

```text
keyboard debug start
```

Physical keys should not add events to the virtual test sequence. After:

```text
keyboard debug stop
```

physical keyboard input should work again.

## Recovery

If a test is interrupted while a modifier is held, issue:

```text
keyboard debug stop
```

The controller releases all virtual keys before leaving debug mode. If the
keyboard UART or either MCU was reset during a request, reset both applications
before continuing so the EF800Application translator and controller virtual
state start from the same clean state. Capture the console log for any failed
cleanup.

## Native and firmware verification

The authoritative current test count and migration gates are in
`docs/migration-status.md`.

Run the native protocol suite before hardware testing:

```powershell
docker exec -e ZEPHYR_BASE=/workspace/zephyr -w /workspace EF800_Devcontainer /opt/venv/bin/west twister -T application/tests -p native_sim --inline-logs --no-clean
docker exec -w /workspace EF800_Devcontainer application/build/keyboard-protocol/zephyr/zephyr.exe
```

The closeout gate is the native suite, all four pristine firmware builds, and
`verify-images.sh`. The physical debug-keyboard test passes when semantic names
exercise normal application behavior, held modifiers work, unknown names are
silent, debug stop releases every virtual key, and physical input resumes.
## DebugConsole keyboard package upload

When USB MSD is unavailable, use the MCUmgr filesystem transport on the
selected DebugConsole. MCUmgr owns the DebugConsole binary stream while the
transport is active; the interactive shell is paused during this operation.
The shell is available by default. Enter `mcumgr start` on the EF800Application
shell before starting the raw serial SMP host; reset EF800Application after the
transfer to restore shell access.

The package is uploaded to:

```text
/EF800:/fwu/EF800_KEYBOARD.pkg
```

The MCUmgr host must use raw serial SMP at `115200 8N1` on the selected
DebugConsole. The equivalent host operation is:

```text
fs upload EF800_KEYBOARD.pkg /EF800:/fwu/EF800_KEYBOARD.pkg
```

After the upload completes, reset EF800Application to validate and process the
package. The package header and payload CRC are checked before the keyboard
controller update starts.

The old `keyboard upload`/`EFKB` command is retained only as a legacy
diagnostic path and must not be used for production transfer testing.

The package path remains:

```text
/EF800:/fwu/EF800_KEYBOARD.pkg
```

The hwdebug image returns `K` only after both the transfer CRC and the package
header/payload CRC have passed. It returns `E` and retains the file when either
validation fails. Successful console output includes:

```text
keyboard receive: package upload successful
```
