# Virtual EF800 web front panel

The interactive front panel runs as a browser service. The simulator and web
gateway are separate containers; `EF800_Devcontainer` remains the development
and firmware-test environment, not the runtime container. The simulator uses a
minimal 32-bit Debian/glibc runtime matching the dynamically linked
`native_sim` executable.

## Start with Docker Compose

Build the Linux `native_sim` executable in the reusable development container,
then build and start the small Alpine runtime and web gateway from the
repository root:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-application -d application/build/ef800-application-native-sim
docker compose -f docker-compose.virtual-ef800.yml build
docker compose -f docker-compose.virtual-ef800.yml up -d
Start-Process http://127.0.0.1:8080/
```

The simulator's raw TCP port `8800` is private to the Compose network. Only the
web service is published, on `127.0.0.1:8080` by default. Override the browser
port with `VIRTUAL_EF800_WEB_PORT=8081`.

The browser initially joins as a viewer. Select **Acquire control** before using
the keypad. Only one browser can control the virtual EF800 at a time; other
browsers remain viewers. Releasing control, closing the browser, or losing the
simulator connection releases all held keys.

The LCD transport carries the frame sequence and packed 240 x 64 one-bit pixel
buffer only. Display characters are visible because the production T6963 path
draws them into those pixels; no parallel semantic text rows are sent or drawn
over the emulated LCD.

Native-sim measurement packages are managed through the MTP `Plugins` storage
in the file view below. Plugin discovery is startup-only; after uploading a
package, request a simulator restart separately if it should be discovered.
Port 1 and Port 2 still support bounded virtual serial response profiles for
plugin demonstrations.

## EF800 Application MTP file view

The browser's **EF800 Application MTP files** panel exposes only the `Data`,
`Plugins`, and `FWU` storages. It omits Configuration, boot-state NVS, and raw
flash. On a clean simulator profile, acquire keypad control and select
**Settings / Port assignment / USB Port / USB MTP** on Port 4 before browsing.

Listing, upload, and download requests go from the browser to the localhost
gateway, through bounded simulator-control messages, and then through the
in-process Zephyr USB host, native-sim virtual UHC/bus/UDC, and production
EF800 Application device-next MTP class. No gateway route reads or writes the
simulator filesystem directly for these file operations. Browser transfers are
limited to 32 KiB per file; paths are relative to the selected storage, and
transfer progress, cancellation, completion, and failure are reported. Download
success requires a complete byte count and checksum.

The local gateway routes are `GET /api/mtp/list?storage=Data&path=...`,
`GET /api/mtp/download?storage=Data&path=...`, and `POST /api/mtp/upload` with
multipart `storage`, `path` (destination directory), and `file` fields. The
browser uses only these gateway routes and `/ws`; the simulator endpoint remains
private to the Compose network.

For scripted plugin activation, the separate `POST /api/simulator/restart`
gateway route remains available; it is intentionally not part of the notebook's
MTP file panel.

The native-sim software USB path is behavioral evidence only. Physical QSPI
qualification, the complete EF800 Backup MTP workload/stack measurement,
and target flashing are not included in this verification increment.

```powershell
docker compose -f docker-compose.virtual-ef800.yml ps
curl http://127.0.0.1:8080/health
docker compose -f docker-compose.virtual-ef800.yml logs -f virtual-ef800-web
docker compose -f docker-compose.virtual-ef800.yml down
```

The health endpoint reports web availability independently from simulator
connectivity. It never opens a second simulator client connection.

The browser also shows a bounded **EF800 Application logs** panel. Native-sim log
records are replayed from recent history on connection and then streamed live over
the existing simulator/gateway connection. The panel supports severity filtering,
pause, clear, and auto-scroll; it marks sequence gaps and simulator diagnostic
epochs. The native history is limited to 64 records and approximately 8 KiB of
text, with 120-byte records. This is behavioral native-sim evidence only and is not
physical console or hardware qualification evidence.

The same panel includes catalog-backed **COMPOSITION** selectors. Acquire browser
control to choose and commit a supported option; the panel shows the currently
active selection separately from the committed selection and reports when a
simulator restart is required. Controls use bounded structured protocol
messages rather than forwarding arbitrary shell text. Unknown or unsupported
catalog entries are not offered for selection.

## Headless client

For deterministic protocol scenarios, run the simulator without the web
service and use:

```powershell
python GSE/Virtual-EF800/scenario_client.py down right
```

Virtual EF800 is behavioral evidence only; it does not qualify STM32 registers,
ARM loading, MPU protection, boot handoff, electrical timing, QSPI, USB, or
fitted peripheral wiring.
