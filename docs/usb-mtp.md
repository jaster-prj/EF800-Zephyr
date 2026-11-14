# EF800 USB MTP

EF800's device-next MTP class is supplied by the standalone
[`jaster-prj/zephyr-mtp`](https://github.com/jaster-prj/zephyr-mtp) module,
pinned by the manifest at `2ddd1097a72db770975a3a91c8c86648ebb5b587`. It is
based on Zephyr PR #86832 and is retained until an equivalent implementation is
available in the selected Zephyr release.

## Visible storage

EF800 Application exposes three named, independent MTP storages:

```text
Data     /DATA:/data
Plugins  /DATA:/plugins
FWU      /DATA:/fwu
```

EF800 Backup recovery exposes four:

```text
Configuration  /EF800:/.configuration
Data           /DATA:/data
Plugins        /DATA:/plugins
FWU            /DATA:/fwu
```

The boot-state NVS and internal-flash partitions are never exposed. The shared
volume root and other unapproved paths are not exported. Each MTP instance has
a bounded object-handle table (12 handles); the native gateway limits the
browser file view to 12 immediate entries per request.

Uploads are staged by the MTP class as private temporary files. A final name is
published only after the declared transfer is complete, synchronized, and
closed. Cancellation, disconnect, malformed transfers, and startup orphan
cleanup must not report or leave a partial upload as a completed file.

## USB personality and ownership

None, USB DebugConsole, and USB MTP are runtime-switchable. Port 4 selects MTP
through the application assignment UI. Entering or leaving USB MSC persists the
selection and requires a controlled restart so raw MSC and filesystem ownership
cannot coexist in one boot. `USBMSD0` disables Main MSC; `USBMSD1` enables it.
Physical EF800 defaults to `USBMSD1`; select `usbmsd mode USBMSD0` on the
EF800 Application shell and restart to disable Main MSC. Native-sim defaults to
`USBMSD0` and does not compile Main MSC.

MTP accesses approved roots through Zephyr FS/FatFS APIs. Configuration FAT and
Shared Data FAT remain separately mounted. EF800 Application and EF800 Backup
must not mount the same shared disk simultaneously. No MTP personality exposes
raw sectors.

## Native-sim and browser gateway

Native-sim runs the same EF800 Application MTP context and production class over
Zephyr's virtual UDC/UHC bus. The in-process software USB host peer performs the
MTP transactions from a Zephyr worker thread; the simulator's POSIX front-panel
socket thread only queues bounded protocol messages. This context boundary is
required for USB host APIs to complete correctly.

The web gateway mediates file list, upload, download, progress, completion,
failure, and cancellation messages. It exposes only Data, Plugins, and FWU, and
applies relative-path checks plus a 32 KiB per-file limit to browser transfers.
MTP payload chunks are bounded to 2,000 bytes inside the 2,400-byte simulator
protocol frame. Download completion requires the full byte count and CRC to
match; uploads are not reported successful until the simulator confirms the
same. The browser connects only to the gateway, never directly to the simulator
control socket or USB host controller. See `docs/virtual-ef800.md` and
`GSE/Virtual-EF800/README.md` for the HTTP API and browser workflow.

Native-sim and software USB tests are behavioral evidence, not physical USB or
storage qualification. Initial Windows/Linux MTP interoperability has been
observed on the engineering target, but the complete physical transfer,
interruption, ownership, and recovery matrix remains a hardware gate. Physical
target programming and QSPI qualification were not part of this software change.

## Removing the temporary module

When official Zephyr MTP support is adopted, remove the `zephyr-mtp` project
from `manifest-repo/west.yml`, remove the compatibility module configuration,
and migrate the application to the official API in one change. The module's
CMake guard intentionally fails if Zephyr begins providing the same public MTP
header, preventing accidental duplicate USB classes.
