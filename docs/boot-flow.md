# EF800 boot flow

The EF800 bootloader is a second-level bootloader entered by the hardware
bootloader. It owns update installation and application selection.

After the QSPI hardware and package-policy gates are closed, the bootloader
will:

1. Initialize the QSPI FAT disk.
2. Find `/fwu/EF800_FW.bin`.
3. Validate the update package and main-image vector table.
4. Program and verify the main application partition.
5. Remove the update file only after successful verification.
6. Start the main application when it is valid.
7. Select the backup application after repeated main-application watchdog
   failures.

Boot failure state transitions are:

```text
MAIN + watchdog reset         -> increment failure counter
MAIN + normal reset/shutdown  -> clear failure counter
failure limit reached         -> start BackupApplication
BACKUP + next boot            -> clear counter and retry MainApplication
```

BackupApplication is therefore a one-boot recovery path. It does not become a
permanent boot target merely because the persistent failure counter reached the
configured limit.

The bootloader always compiles the QSPI/FAT update path. A missing or
unavailable QSPI volume is handled as a runtime condition and does not prevent
normal main/backup selection.

The first Zephyr implementation uses a small custom boot manager to preserve
the legacy internal backup application. MCUboot may be evaluated later, but it
cannot be adopted without changing the fixed backup-image contract.
