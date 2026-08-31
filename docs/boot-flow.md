# EF800 boot flow

The EF800 bootloader is a second-level bootloader entered by the hardware
bootloader. It owns update installation and application selection.

1. Initialize the QSPI FAT disk.
2. Find `/fwu/EF800_FW.bin`.
3. Validate the update package and main-image vector table.
4. Program and verify the main application partition.
5. Remove the update file only after successful verification.
6. Start the main application when it is valid.
7. Select the backup application after repeated main-application watchdog
   failures.

The first Zephyr implementation uses a small custom boot manager to preserve
the legacy internal backup application. MCUboot may be evaluated later, but it
cannot be adopted without changing the fixed backup-image contract.
