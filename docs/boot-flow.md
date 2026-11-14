# EF800 boot flow

The EF800 bootloader is a second-level bootloader entered by the hardware
bootloader. It owns update installation and application selection.

After the QSPI hardware and package-policy gates are closed, the bootloader
will:

1. Initialize the QSPI FAT disk.
2. Find `/fwu/EF800_FW.bin`.
3. Validate the update package and main-image vector table.
4. Program and verify the EF800 application partition.
5. Remove the update file only after successful verification.
6. Start the EF800 application when it is valid.
7. Select the backup application after repeated main-application watchdog
   failures.

Boot failure state transitions are:

```text
MAIN + watchdog reset         -> increment failure counter
MAIN + normal reset/shutdown  -> clear failure counter
failure limit reached         -> start EF800 Backup
BACKUP + next boot            -> clear counter and retry EF800 Application
```

EF800 Backup is therefore a one-boot recovery path. It does not become a
permanent boot target merely because the persistent failure counter reached the
configured limit.

The bootloader always compiles the QSPI/FAT update path. A missing or
unavailable QSPI volume is handled as a runtime condition and does not prevent
normal main/backup selection.

The first Zephyr implementation uses a small custom boot manager to preserve
the legacy internal backup application. MCUboot may be evaluated later, but it
cannot be adopted without changing the fixed backup-image contract.
# Keyboard controller watchdog recovery

Keyboard firmware packages use their target-specific names:

```text
/EF800:/fwu/EF800_KEYBOARD_UPDATER.pkg
/EF800:/fwu/EF800_KEYBOARD_BOOT.pkg
/EF800:/fwu/EF800_KEYBOARD_APP.pkg
```

Each file is a target-bound EFW1 package. The application image is linked at
`0x08008000`; the resident keyboard bootloader occupies
`0x08000000..0x08007fff`.

EF800 Application reports staged update outcomes with the `firmware_update_status`
shell command. Retry-required targets should be inspected before restaging; a
Bootloader target reported unsupported remains ST-LINK-only until its separate
flash-safety qualification is complete.

The STM32L431 keyboard controller has a resident bootloader and a relocated
application. The bootloader keeps a CRC-protected handoff record in the
reserved top 4 KiB of SRAM. Before starting the application it marks the
record `PENDING`. The application marks it `HEALTHY` after UART, matrix scan,
startup `0xaa`, and three scan cycles are working.

If the bootloader observes an IWDG reset while the previous application state
was `PENDING`, it increments the watchdog-start counter. On the third
consecutive watchdog reset it does not start the application and jumps to the
STM32 system bootloader. A power-on or brownout reset invalidates retained SRAM
state. The application must not clear the counter merely because it started.

`CMD_KEYBOARD_BOOTLOADER` remains the normal update entry. The application
stores a system-bootloader request and resets; the resident bootloader then
jumps to system memory before the application starts its software watchdog.

The resident bootloader software-starts the IWDG with a 10-second timeout. The
keyboard application never starts or reconfigures it; it only feeds the
inherited watchdog through `ef800_watchdog_feed()`.

The keyboard board does not use an option-byte or hardware-started IWDG. The
watchdog owner is therefore unambiguous: the resident bootloader starts it,
and the application inherits it through `ef800_watchdog_inherit()`.

The STM32L431 IWDG cannot be disabled once started. Consequently, there is no
valid software operation that turns it off before the factory system-memory
bootloader. The resident loader feeds it immediately and performs the ROM jump
without delay. The ROM update session must complete within the watchdog window;
the future ROM transport must either refresh the watchdog safely while the ROM
loader is active or use an update-specific hardware recovery strategy. A claim
that the watchdog is “turned off” after startup would be incorrect.
