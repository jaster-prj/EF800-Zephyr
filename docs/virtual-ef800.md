# Virtual EF800

The virtual EF800 is a Zephyr `native_sim` behavioral target. It does not
emulate STM32 registers or require EF800 hardware. The interactive deployment
runs in its own Compose-managed simulator container.

The native target executes the production MainApplication composition root.
GPIO, UART, I2C, RTC, battery, MCP23017, T6963, keyboard, and FAT-storage
dependencies are supplied by project-local native-sim emulators or Zephyr
simulators. The default profile is healthy and uses disposable storage.

Build it from the container workspace:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-main -d application/build/ef800-main-native-sim
```

The standalone deployment builds the simulator image and a browser gateway:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-main -d application/build/ef800-main-native-sim
docker compose -f docker-compose.virtual-ef800.yml build
docker compose -f docker-compose.virtual-ef800.yml up -d
Start-Process http://127.0.0.1:8080/
```

The simulator front-panel transport listens on TCP port `8800` only inside the
private Compose network. The web gateway owns that one binary connection and
serves the browser on `127.0.0.1:8080`. Multiple browsers may view the display,
but only one explicit controller can send keypad events. Stop with:

```powershell
docker compose -f docker-compose.virtual-ef800.yml down
```

## MainApplication diagnostics

The browser front panel includes a bounded **MainApplication logs** panel for the
native-sim target. It receives startup and runtime records through the existing
gateway-owned front-panel connection, replays the simulator's recent history after
connection, and then follows live records. Records carry severity and sequence
metadata; gaps, queue overflow, and simulator reconnects are shown as diagnostic
discontinuities rather than being silently filled.

The native simulator retains at most 64 recent records and approximately 8 KiB of
log text, with each record bounded to 120 bytes. Browser clear, pause, severity
filter, and auto-scroll controls affect only the browser view. They do not erase
simulator history or change the sequence counter. The stream is deliberately
non-durable and does not guarantee delivery across a simulator restart.

These logs are native-sim behavioral diagnostics only. They do not qualify physical
console routing, STM32 behavior, ARM package loading, MPU containment, electrical
timing, or fitted peripheral wiring.

## Native-sim plugin upload

The browser deployment accepts native-sim `.PMP` packages without switching
USB mode. Uploading is bounded and writes through the simulator control
connection into the virtual FAT `/plugins` directory. A successful upload does
not hot-load code; use the separate **Restart simulator** action to activate it
through normal startup discovery.

The local gateway endpoints are:

```text
POST /api/plugins
POST /api/simulator/restart
POST /api/devices/<1|2>
```

Only native-sim packages are accepted. Physical ARM packages remain separate
artifacts and are rejected by the native package target/loader. Virtual device
profiles configure bounded serial response bytes, delay, availability, and
errors for docked plugin-port tests.

The `native_sim` executable is built as a dynamically linked ELF 32-bit i386
Linux binary inside `EF800_Devcontainer`. The runtime uses the matching
`i386/debian:bookworm-slim` glibc image and packages only the binary plus the
non-root user metadata. Its required runtime libraries are `/lib/ld-linux.so.2`,
`libc`, and `libm`.

Direct headless clients may send bounded profile-control messages through the
internal simulator protocol. Serial peer controls support response bytes,
delay, availability, and bounded error values. The browser gateway does not
expose unrestricted profile control.

For isolated runs, start the native simulator with `--flash_in_ram`; the
default profile is disposable. Persistent simulator storage is explicit:

```text
zephyr.exe --flash=/path/to/ef800-virtual.flash
```

Never point this option at a physical EF800 image.

Run the startup test with the normal native test collection:

```powershell
docker exec -e ZEPHYR_BASE=/workspace/zephyr -w /workspace EF800_Devcontainer /opt/venv/bin/west twister -T application/tests -p native_sim --inline-logs --no-clean
```

Virtual results cover behavioral application code only. They do not qualify
STM32 register behavior, the boot chain, MPU protection, ARM fault handling,
electrical timing, QSPI, USB, or fitted peripheral wiring.
