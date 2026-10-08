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
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-main -d application/build/ef800-main-native-sim
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

The front panel also provides native-sim plugin upload and a separate simulator
restart action. Uploading a `.PMP` package validates and installs it into the
simulator's virtual FAT; it is not activated until restart. Port 1 and Port 2
can be configured with bounded virtual serial response profiles for plugin
demonstrations.

```powershell
docker compose -f docker-compose.virtual-ef800.yml ps
curl http://127.0.0.1:8080/health
docker compose -f docker-compose.virtual-ef800.yml logs -f virtual-ef800-web
docker compose -f docker-compose.virtual-ef800.yml down
```

The health endpoint reports web availability independently from simulator
connectivity. It never opens a second simulator client connection.

The browser also shows a bounded **MainApplication logs** panel. Native-sim log
records are replayed from recent history on connection and then streamed live over
the existing simulator/gateway connection. The panel supports severity filtering,
pause, clear, and auto-scroll; it marks sequence gaps and simulator diagnostic
epochs. The native history is limited to 64 records and approximately 8 KiB of
text, with 120-byte records. This is behavioral native-sim evidence only and is not
physical console or hardware qualification evidence.

## Headless client

For deterministic protocol scenarios, run the simulator without the web
service and use:

```powershell
python GSE/Virtual-EF800/scenario_client.py down right
```

Virtual EF800 is behavioral evidence only; it does not qualify STM32 registers,
ARM loading, MPU protection, boot handoff, electrical timing, QSPI, USB, or
fitted peripheral wiring.
