# EF800 QSPI layout migration

## Safety rule

The new firmware does not convert the existing 4 MiB monolithic FAT volume in
place. The new NVS, Configuration, and Shared Data regions overlap that old FAT
volume. A failed mount is **not** permission to format, repartition, or write a
boot-state record.

The firmware checks the raw QSPI boot sector for a plausible legacy FAT BPB
before mounting the new filesystems or initializing NVS. On a match, it leaves
the media untouched and skips the Bootloader update scan. MainApplication uses
volatile boot-state defaults and does not mount QSPI-backed configuration or
shared data. BackupApplication also does not expose the legacy volume as raw
MSC. The image reports the layout mismatch; it does not initialize the new
NVS/FAT layout over the old volume.

## Recoverable service procedure

Physical QSPI qualification and migration are currently release-gated. Do not
run this procedure on a target until the fitted part, capacity, erase geometry,
read path, and a partition-aware initializer have been qualified.

1. Keep the legacy firmware installed. Disconnect USB hosts that could write
   the volume and stop applications that use it.
2. Read and save a complete raw 4 MiB QSPI image with an approved read-only
   programmer. Record its SHA-256 and verify the image size is exactly
   4,194,304 bytes. Separately copy `.configuration`, `data`, `plugins`, and
   `fwu` through the legacy filesystem interface; record file sizes and hashes.
3. Verify that both backups are readable and that the raw image can be restored
   to a spare or otherwise recoverable device. Keep the original image immutable.
   If a complete verified backup cannot be made, stop; do not install the new
   partition map on that device.
4. With the verified backup secured, explicitly erase the entire QSPI part using
   the qualified service fixture. Initialize the 32 KiB boot-state NVS at
   `0x000000`, then separately format the 992 KiB Configuration FAT at
   `0x008000` and 3072 KiB Shared Data FAT at `0x100000`. Do not use
   MainApplication's mount path or the existing `hwdebug` storage test as a
   migration formatter.
5. Install the matching Bootloader, BackupApplication, and MainApplication
   images. Confirm the legacy-layout warning is absent, the boot-state record
   can be initialized, and both FAT volumes mount without formatting on mount
   failure. Validate BackupApplication file-level MTP recovery access without
   enabling raw MSC for mounted volumes.
6. Restore `.configuration` contents to Configuration, and restore `data`,
   `plugins`, and `fwu` to their corresponding Shared Data directories. Verify
   each restored file against the recorded size/hash and test a second readback.
   Keep the host backup until the device passes the complete recovery and
   filesystem checks.

If any post-erase step fails, restore the saved raw image and the matching
legacy firmware. Do not boot legacy firmware against the new partition layout;
it expects one FAT volume beginning at QSPI offset zero. This migration is an
explicit full-device replacement after backup, not automatic in-place
repartitioning. The software regression test synthesizes a legacy FAT boot
sector and verifies that metadata initialization, both new-volume sentinels,
and the original boot sector remain unchanged. It does not qualify a physical
QSPI part or service fixture.

This procedure is a documented service plan, not evidence that the fitted
QSPI part or a physical migration has been qualified. Do not flash a target as
part of the software-only change closeout.
