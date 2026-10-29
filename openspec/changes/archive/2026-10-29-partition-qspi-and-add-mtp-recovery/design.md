## Context

MainApplication and the Bootloader currently share one 4 MiB QSPI FAT volume. MainApplication mounts it for `.configuration`, `data`, `plugins`, and `fwu`; BackupApplication exports the whole disk through MSC; the Bootloader mounts the same volume to find `fwu/EF800_FW.bin`. MainApplication MTP is being added by the in-progress `add-hot-switchable-usb-mtp` change, but its native-sim adapter currently returns `-ENOTSUP`. The web gateway's existing plugin upload writes directly to the simulator filesystem and does not exercise USB.

The current internal-flash map reserves 32 KiB for NVS boot metadata between the 96 KiB BackupApplication and MainApplication at `0x08030000`. The external QSPI is a 4 MiB part, but its complete partition geometry and migration from the current FAT volume remain hardware qualification gates. Existing ADRs require MainApplication MTP to use approved filesystem roots, preserve the Main MSC restart/ownership boundary, execute the production MainApplication on native-sim, keep the browser behind the gateway, and retain restart-scoped composition profiles.

## Goals / Non-Goals

**Goals:**

- Separate boot-state NVS, private configuration, and shared user/update data on QSPI using the selected 32 KiB / 992 KiB / 3072 KiB layout.
- Keep configuration mounted independently of shared-data MSC handoff, eliminating the need to mirror application port assignments in boot metadata.
- Give BackupApplication a file-level MTP recovery surface for Configuration, Data, Plugins, and FWU, without exposing raw NVS or internal-flash sectors.
- Keep the Bootloader's automatic updater limited to validated MainApplication images; keep second-level Bootloader updates ST-LINK-only.
- Exercise MainApplication's real device-next CDC and MTP contexts in native-sim, and let the localhost browser operate Main MTP through an in-process software USB host.
- Gate MainApplication MSD availability through target composition: `USBMSD0` for native-sim and default `USBMSD1` for the physical target.

**Non-Goals:**

- Programming or qualifying a physical EF800 target in this change.
- Updating the second-level Bootloader from an uploaded file.
- Automatically installing BackupApplication images through the Bootloader.
- Treating native-sim USB tests as Windows/Linux host interoperability or STM32 electrical qualification.
- Exposing the external boot-state NVS as an MTP or MSC filesystem.

## Decisions

### Partition external QSPI into NVS, configuration FAT, and shared-data FAT

Use the full 4 MiB address space as three erase-aligned regions:

```text
0x000000 - 0x007FFF   32 KiB   boot-state NVS
0x008000 - 0x0FFFFF  992 KiB   configuration FAT
0x100000 - 0x3FFFFF    3 MiB   shared-data FAT
```

The NVS partition stores the boot-state record only. Configuration FAT retains `.configuration` files such as composition profiles, port assignments, time settings, and workspace selection. Shared-data FAT contains `data`, `plugins`, and `fwu`. Keep the logical configuration mount at `/EF800:` so existing configuration paths can remain stable; mount shared data at a separate path and update its consumers. The Bootloader reads boot state from NVS and mounts only shared data when checking for `fwu/EF800_FW.bin`.

Alternatives considered:

- Keep one shared FAT volume: rejected because configuration would be unavailable or raw-owned whenever the data volume is handed to MSC.
- Put the boot counter in a FAT file: rejected because boot accounting should not depend on FAT directory/file mutation semantics; NVS provides bounded key/value updates and recovery.
- Expose the NVS as raw MTP/MSC sectors: rejected because host writes could corrupt the boot-state journal.

### Keep configuration application-owned and hand off only shared data

MainApplication mounts the configuration FAT throughout its runtime. MainApplication MTP accesses only approved roots from shared data while the application owns that filesystem. MainApplication MSC, when the effective composition is `USBMSD1`, unmounts only the shared-data FAT and exposes only its block device after the existing restart boundary. BackupApplication runs separately and mounts both FAT regions for MTP recovery; it does not expose MSC while those filesystems are mounted.

The Main MTP surface remains Data, Plugins, and FWU, consistent with ADR-0011's privacy boundary. The distinct Backup recovery MTP surface adds Configuration explicitly, allowing repair of private settings only while BackupApplication owns the device. Both MTP contexts use filesystem APIs and never export raw sectors.

### Relocate boot state to external NVS and retire the internal metadata partition

Use the 32 KiB QSPI NVS region for boot-state records. If the record is invalid or absent but the region is accessible, initialize the boot-state record to defaults and boot a valid MainApplication. If QSPI/NVS access itself fails, use volatile defaults and attempt MainApplication without formatting either FAT region. Report the missing persistent counter; do not make a boot-state failure destructive to user data.

Remove the internal metadata partition from the image map and expand BackupApplication from 96 KiB to 128 KiB, preserving MainApplication's `0x08030000` link address. The resulting internal map is Bootloader 64 KiB, BackupApplication 128 KiB, and MainApplication 832 KiB. Port assignments and composition are stored in the always-mounted configuration FAT, so no internal assignment mirror is needed for Main's data-volume handoff.

Alternatives considered:

- Keep internal NVS and mirror QSPI configuration: rejected because the selected storage model has a dedicated always-mounted configuration volume and a separate boot-state NVS region.
- Move the MainApplication link address to reclaim 32 KiB: rejected because preserving the current Main image address reduces compatibility churn.

### Keep the automatic installer MainApplication-only

The Bootloader reads `EF800_FW.bin` from the shared-data `fwu` directory, validates the supported MainApplication format/vector/bounds/integrity, writes only the Main partition, verifies it, and removes the consumed file best-effort. It does not update BackupApplication and never self-programs the second-level Bootloader. The latter remains ST-LINK-only; BackupApplication replacement remains a separate service procedure.

### Make BackupApplication a recovery MTP device

Use the existing pinned Zephyr MTP class module in BackupApplication. Its MTP storages map to the Configuration directory and the Data, Plugins, and FWU roots. Keep object and transfer bounds; incomplete uploads must not become consumable update files. Keep the external NVS inaccessible through MTP. BackupApplication's 128 KiB image budget and RAM/stack headroom must be verified before closing the change.

### Exercise MainApplication MTP through Zephyr's virtual USB bus

Replace the native-sim `-ENOTSUP` MTP adapter with a target-neutral Main MTP context and a physical-only clock-recovery adapter. Build the native-sim application with Zephyr's device-next virtual UDC/UHC pair. An in-process software host peer will enumerate the actual Main MTP device and implement the bounded MTP operations required by the browser file view. CDC uses the same virtual bus for deterministic enumeration/data tests. Browser HTTP/WebSocket requests go through the gateway's existing single simulator connection; the gateway does not read or write FAT directly.

The browser MTP panel covers MainApplication's Data, Plugins, and FWU storages. Configuration remains hidden in MainApplication MTP and is available only through BackupApplication recovery MTP. Physical Windows/Linux and electrical qualification remain separate gates.

### Use target-specific composition defaults for Main MSD availability

Register `USBMSD0` as the native-sim composition and `USBMSD1` as the physical EF800 default. Native-sim must not compile/activate Main MSC; it omits the MSD assignment from Port 4 and clears any stale stored MSD assignment. The physical `USBMSD1` path retains the current MSC ownership/restart behavior. A profile token unsupported by the current target cannot override its build's safe default.

## Risks / Trade-offs

- [The current QSPI FAT volume overlaps the proposed NVS/config/data regions] → Require an explicit verified backup-and-migration procedure; never infer that a failed mount authorizes formatting or repartitioning.
- [Boot-state persistence now depends on external QSPI] → Reset invalid NVS state only within its dedicated 32 KiB region, boot Main with defaults when QSPI state is unavailable, and retain diagnostics; qualify QSPI initialization and power-loss behavior before target deployment.
- [BackupApplication gains an MTP stack inside a 128 KiB image] → Measure image size, RAM, and thread-stack high-water in all firmware builds before considering target programming.
- [Two FAT volumes and multiple MTP roots can be confused by host tooling] → Use stable disk/mount/storage names and test each root against the intended partition.
- [Browser MTP requires a software MTP peer, not just virtual USB controllers] → Keep the peer protocol narrow, test device-next transactions through the real MTP class, bound all gateway messages, and do not claim desktop-host interoperability.
- [Composition may contain old or cross-target tokens] → Preserve unknown tokens, apply only tokens supported by the current target catalog, and automatically clear stale native-sim MSD assignments without enabling MSC.
- [Bootloader/Backup image migration changes the deployed internal layout] → Preserve the Main link address, provide explicit ST-LINK/service deployment steps, and defer physical migration until a complete backup and target qualification plan exists.

## Migration Plan

1. Implement and test the partition map and separate mount adapters in native-sim without touching a physical QSPI device.
2. Provide an explicit legacy single-volume backup/migration path that preserves `.configuration`, `data`, `plugins`, and `fwu` files; no automatic in-place repartitioning is allowed.
3. Implement external QSPI NVS boot-state reads/writes and default behavior, then update the Bootloader's Main-only package scan to the shared-data mount.
4. Build BackupApplication recovery MTP and verify the four logical storages, interrupted-transfer behavior, and 128 KiB image bound.
5. Add composition catalog consumers, native-sim virtual UDC/UHC plus CDC/MTP peers, and browser MTP operations through the gateway.
6. Run native USB/filesystem/composition tests, all firmware builds, and image-layout verification. Record that target flashing and physical QSPI/MTP qualification remain deferred.

Rollback before physical migration restores the previous firmware set and leaves the original QSPI image untouched. Once QSPI is repartitioned, rollback requires restoring the saved legacy volume through the documented recovery procedure; the old firmware must not mount the new partition layout as its former monolithic FAT volume.

## Open Questions

- Confirm the on-device, power-loss-safe procedure for backing up and migrating the existing single FAT volume before allocating the NVS and two FAT regions.
- Confirm the exact BackupApplication MTP object/transfer limits that fit its 128 KiB internal image and RAM budget.
- Physical QSPI erase geometry, startup biasing, maximum frequency, and MTP host interoperability remain target qualification gates and are not closed by this change's software-only tests.
