# Runtime View

The sequence diagrams below describe important runtime collaborations. Editable
PlantUML sources are stored in
[`../diagram_as_code/06_runtime_view/`](../diagram_as_code/06_runtime_view/README.md).

## Boot selection and Main update

Source: [`boot_update.puml`](../diagram_as_code/06_runtime_view/boot_update.puml).
The Bootloader checks persistent boot state, inspects the optional Main update,
validates image vectors, records its selection, and hands off. Backup is a
recovery boot, not a permanent boot target.

## Main startup and guarded legacy-QSPI reformat

Source: [`main_startup_qspi.puml`](../diagram_as_code/06_runtime_view/main_startup_qspi.puml).
The validated legacy-volume check precedes boot-state reads and normal storage
mounts. Only a recognized legacy FAT signature plus the development-only
`CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI` option triggers formatting. A normal FAT
mount error is not a trigger. The operation destroys old data; production images
must disable the option. See [`../../docs/qspi-migration.md`](../../docs/qspi-migration.md).

## Measurement through an external package

Source: [`measurement_plugin.puml`](../diagram_as_code/06_runtime_view/measurement_plugin.puml).
The UI resolves a configured port and plugin, the host activates a package and
routes its instrument request through the host transport service, then validates
the result and owns workspace persistence and lifecycle cleanup.

## Browser upload through Virtual EF800 MTP

Source: [`virtual_mtp_upload.puml`](../diagram_as_code/06_runtime_view/virtual_mtp_upload.puml).
Browser operations cross the gateway, bounded simulator messages, in-process
USB host peer, virtual UHC/UDC bus, production MTP class, and Zephyr filesystem.
The gateway is not a direct filesystem shortcut.

**Known caveat:** the virtual MTP device may report startup success while the
simulated USB host times out during MTP interface enumeration. This diagram
describes the intended collaboration, not proof that all enumeration and
transfer cases pass. Physical MTP is a separate, incomplete qualification gate.
See [`../../docs/usb-mtp.md`](../../docs/usb-mtp.md).

## FAT to physical MSC ownership transition

Source: [`storage_ownership.puml`](../diagram_as_code/06_runtime_view/storage_ownership.puml).
MTP remains filesystem-mediated. MSC is raw shared-disk ownership and is allowed
only after FAT unmount; failed startup rolls back to FAT. Where USB worker
quiescence is uncertain, MSC-to-FAT requires a controlled restart.
