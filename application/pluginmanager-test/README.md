# PluginManager NUCLEO-G0B1RE Host

This application is the NUCLEO host for the EF800 measurement-plugin SDK at
`../../plugins/measurement-plugin` relative to `src/main.c`. The build produces Cortex-M0+ ELTA3 and
FTS3XX packages with the active Zephyr ARM toolchain, verifies them with the
SDK tools, and embeds every byte of both `.pmp` files via `binary_to_c.py`.
PluginManager is obtained from the pinned west project by default. The
configurable `PLUGIN_MANAGER_DIR` cache path remains available for framework
development against a sibling checkout.

This host prototypes application-owned action discovery. `plugin menu` lists
assigned Measurement actions, marking missing packages and unavailable routes;
`plugin assign <2|3> 2` selects the USART1 binding, and `plugin assign <2|3> 0`
disables the action. These assignments are RAM-only. A ready UART route does
not prove that an instrument is physically attached. Measurement and optional
directional/enter actions share the selected binding.

This application is an integration proof, not the production MainApplication.
It proves the PluginManager package, filesystem, lifecycle, UART device, and
bounded calculation paths on the NUCLEO-G0B1RE. Production EF800 work still
includes MainApplication integration, persistence, USB MSC storage ownership,
UI services, package trust, executable-memory protection, and update rollback.

The separately registered static Calculation plugin demonstrates a second
interface through the same manager. `calc status` shows its action;
`calc preview <offset-mm>` reads at most 512 bytes of `/PM:/measurements.dat`
(signed millimetre readings, one per line) and returns transformed text without
writing. `calc save <offset-mm> confirm` creates
`/PM:/measurements-offset.dat` only when absent, via a temporary file and rename.
Oversize input/output and
malformed readings fail without truncation. This simplified NUCLEO format is
not EF800's production Workspace record format.

At boot the bring-up image never erases or formats the external flash
partition. It mounts the existing FAT volume at `/PM:` without formatting and
loads package files from the volume root: `/PM:/elta3.pmp` and
`/PM:/fts3xx.pmp`. The previous `/PM:/plugins` directory layout is not used
because the target FatFS volume has a short-name limitation with that
directory. `plugin reinstall-examples` is the explicit destructive replacement
command and must only be used when the external volume may be overwritten.

Build in the persistent EF800 container:

```powershell
docker exec -w /workspace EF800_Devcontainer `
  /opt/venv/bin/west build -p always -b nucleo_g0b1re `
  application/application/pluginmanager-test `
  -d application/build/pluginmanager-test `
  -DPLUGIN_MANAGER_DIR=/workspace/PluginManager
```

On the tested board, USART2 is the console through the NUCLEO ST-LINK virtual
COM port (`COM14`) and USART1/`arduino_serial` is the PluginManager Port service
through the CP210x bridge (`COM7`). Selecting a plugin
reads its SDK defaults and configures USART1 at runtime: ELTA3 uses `1200 7O2`
and FTS3XX uses `9600 8N1`.
The application resets USART state and owns prescaler and clock configuration
on every plugin initialization; plugins do not control those hardware details.

Useful shell commands:

```text
plugin list
plugin menu
plugin assign 2 2
plugin select 2
plugin select 3
plugin status
plugin storage-check
plugin unload
plugin rescan
plugin reinstall-examples
port status
kb measure
kb up
kb down
kb enter
kb cancel
calc status
calc demo 5
calc preview 10
calc save 10 confirm
```

The repeatable hardware lifecycle smoke test runs the selection and unload
sequence for both plugins without modifying the external volume:

```powershell
powershell -ExecutionPolicy Bypass -File tests/hardware_acceptance.ps1 -Port COM14
```

The storage-backed milestone is checked separately:

```powershell
powershell -ExecutionPolicy Bypass -File tests/storage_acceptance.ps1 -Port COM14
```

Exit code `0` proves that the mounted FAT files were validated with
`pm_package_parse_reader()`. Exit code `2` is an explicit hardware blocker and
means that the external volume is still unavailable; it is not a formatting or
repair command.

Calculation execution uses an application-owned bounded service. The service
receives a workspace snapshot and returns a bounded output buffer; it has no
filesystem handle or Zephyr type in its contract. Filesystem snapshot and
atomic output-file handling remain host adapters. The host-only regression is
compiled from `tests/host_policy_test.c`, `tests/calculation_service_test.c`,
`src/host_policy.c`, `src/calculation_service.c`, and
`src/calculation_plugin.c`.

The calculation hardware smoke test is:

```powershell
powershell -ExecutionPolicy Bypass -File tests/calculation_acceptance.ps1 -Port COM14
```

The dummy-device measurement test uses the NUCLEO shell on `COM14` and the
device simulator on `COM7`. Start the repository simulator in a separate
terminal first:

```powershell
python application/plugins/measurement-plugin/tools/instrument_simulator.py COM7 --instrument elta3
powershell -ExecutionPolicy Bypass -File tests/measurement_acceptance.ps1 -Instrument elta3
```

For the Fennel-compatible device, use `--instrument fts3xx` and
`-Instrument fennel1`. The simulator must remain running while the acceptance
script executes. The test validates the structured measurement returned by the
plugin, not just UART readiness.

It requires the NUCLEO ST-LINK shell at `115200 8N1` and passes only when both
plugins can be discovered, selected, instantiated, status-checked, and
unloaded. Measurement commands are intentionally separate because they require
an instrument simulator on USART1/COM7. The test never runs `plugin reinstall-examples`
and never formats storage.

Keyboard commands print `KB: Measure`, `KB: Up`, `KB: Down`, `KB: Enter`, or
`KB: Cancel`. The `kb cancel` operation is a host-only no-op because SDK
interface version 1 does not define a cancel event.
FTS3XX does not support the directional/enter inputs and reports that condition
without unloading the instance.

The manager, filesystem attachment, mount, Port service, and shell remain
alive after startup. Access is serialized, and unload/shutdown follows instance,
plugin image, manager, then FAT mount order. With a static Calculation plugin,
PluginManager v1 cannot detach the filesystem independently of manager
destruction. Discovery
is limited to four directory entries and 32 KiB packages. The loader provides
the SDK linker limits exactly: 8 KiB text, 8 KiB rodata, 2 KiB data, and 2 KiB
BSS. The development host executes plugin text from RAM and therefore disables
the MPU.

When the FAT volume is mounted, startup validates both package files using the
PluginManager streaming reader and image CRC before attaching the filesystem to
the manager. `plugin storage-check` repeats this validation on demand. Package
selection then uses `pm_manager_load_available()`, which rereads the selected
package from the mounted storage before loading it into the existing RAM image
buffers. If the volume cannot be mounted, the application remains in degraded
mode and does not format or overwrite it.

For a host-only regression, compile `tests/host_policy_test.c` with
`src/host_policy.c` and `src/calculation_plugin.c` and the public
PluginManager/measurement headers. The board acceptance scripts additionally
verify root-volume package validation, package loading, plugin lifecycle,
calculation invocation, and both dummy-device protocols. The simplified
calculation file format is still not the production EF800 Workspace format.

See `TEST_ENVIRONMENT.md` for SPI wiring and flashing details.
