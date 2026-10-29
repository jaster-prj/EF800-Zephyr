# Use MTP for Concurrent USB File Access with an MSC Restart Boundary

- Status: accepted
- Date: 2026-10-22
- Supersedes: None

## Context

EF800 stores plugins, measurement data, configuration, and update packages on a QSPI FAT volume. USB Mass Storage gives the host raw block access, so MainApplication must unmount FAT and cannot use the filesystem concurrently. The current Zephyr MSC worker also cannot be proven quiescent enough to remount FAT safely in the same boot after MSC shutdown.

Windows and Linux need file-transfer access while MainApplication remains active. USB DebugConsole and a filesystem-mediated file-transfer class can be stopped and re-enumerated without transferring raw disk ownership, but MSC changes the storage ownership model and requires a stronger boundary. Host uploads can also be interrupted and must not replace completed plugin, data, or update files under their final names before transfer completion.

## Decision

EF800 will use USB MTP as its concurrent file-transfer personality. MTP will access the mounted FAT volume only through Zephyr filesystem APIs and will expose only explicitly approved directory roots. Host uploads will be staged privately and atomically promoted to their requested final paths after complete transfer.

A single serialized USB personality manager will own the STM32 USB device controller. It may hot-switch among None, USB DebugConsole, and USB MTP by disconnecting, shutting down, and re-enumerating the selected class without restarting MainApplication. Every transition entering or leaving USB MSC will persist the requested personality and cross a controlled restart boundary; FAT and MSC raw-block access will never be active concurrently or handed back in the same boot.

Until compatible MTP support is part of the selected Zephyr release, the class will be carried as an isolated, pinned Zephyr module rather than as modifications to the Zephyr checkout.

## Consequences

- MainApplication and a Windows or Linux MTP host can access different completed files while FAT remains mounted and reentrant.
- MTP visibility is directory-scoped and does not imply host access to private configuration or the whole FAT volume.
- USB personality changes that do not involve MSC no longer require an EF800 restart, although the host observes disconnect and re-enumeration.
- MSC remains available for block-device compatibility but always requires a restart when entered or left.
- The USB manager, filesystem concurrency policy, atomic upload lifecycle, and host interoperability require native and physical-target tests.
- The temporary module must be removed atomically when official Zephyr MTP support is adopted to avoid duplicate APIs, Kconfig symbols, and USB classes.
