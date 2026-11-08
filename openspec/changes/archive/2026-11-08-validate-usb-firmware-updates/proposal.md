# Proposal

## Why

The keyboard update path cannot rely on the STM32 factory ROM I²C bootloader on the fitted board. A dedicated, resident updater partition provides a stable recovery and programming path that can update the keyboard application and first-stage bootloader without executing from either image being replaced.

## What Changes

- Preserve the validated L496 MainApplication update and keyboard-version diagnostic work.
- Divide the L431's 256 KiB flash into a 32 KiB watchdog/bootloader region, a 192 KiB keyboard-application region, and a 32 KiB updater-application region at the end of flash.
- Keep the keyboard application's existing I²C address (`0x2a`) for diagnostics and updater-only self-update; reserve `0x48` for the dedicated updater service. Both services expose a `VERSION` command that reports the version of the image currently serving that address. Reuse AN4221-style framing where useful, but do not enter or depend on the factory ROM bootloader.
- Keep the keyboard bootloader focused on watchdog/recovery and normal application startup. On an explicit I²C address-only update probe during the startup window, it launches the updater application.
- Let the updater explicitly erase an authorized target partition or block, stream image data directly to its fixed flash destination, and report status/check results. Do not cache the full target image in RAM or another keyboard-flash staging area.
- Permit only the keyboard application to update the updater partition, using the same I²C update framing but an updater-only target policy.
- Keep UART dedicated to keyboard debug input and scancode/event output. Validate bounds, integrity, interruption, recovery, and no-SWD update behavior.

## Capabilities

### New Capabilities

- `keyboard-diagnostics`: MainApplication provides a live keyboard firmware version diagnostic through its shell.
- `keyboard-firmware-update`: the keyboard bootloader, application, and dedicated updater provide bounded and recoverable I²C firmware updates.

### Modified Capabilities

None.

## Impact

This change affects the keyboard flash partitions and linker configuration, keyboard application and bootloader, a new updater application, MainApplication's I²C host and package processing, package tooling, native tests, and hardware-validation documentation. MainApplication stages packages over the existing MTP path. The custom updater uses address `0x48`; this is not a claim that the STM32 factory ROM is available on the fitted bus. The proposed L431 map is bootloader `0x08000000..0x08007fff`, application `0x08008000..0x08037fff`, updater `0x08038000..0x0803ffff`. L496 update work already completed in this change remains in scope and unchanged.
