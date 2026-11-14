# Virtual EF800

The virtual EF800 is a Zephyr `native_sim` behavioral target. It does not
emulate STM32 registers or require EF800 hardware. The interactive deployment
runs in its own Compose-managed simulator container.

The native target executes the production EF800 Application composition root.
GPIO, UART, I2C, RTC, battery, MCP23017, T6963, keyboard, and FAT-storage
dependencies are supplied by project-local native-sim emulators or Zephyr
simulators. The default profile is healthy and uses disposable storage.
The virtual DS3231 I2C device follows native_sim's host-UTC clock. EF800 Application
reads it through the production RTC driver once per second, applies the configured
UTC offset and DST adjustment, and refreshes the LCD header with local `HH:MM`.

Build it from the container workspace:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-application -d application/build/ef800-application-native-sim
```

The standalone deployment builds the simulator image and a browser gateway:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b native_sim application/application/ef800-application -d application/build/ef800-application-native-sim
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

## EF800 Application MTP file view

The browser file panel lists only EF800 Application's named `Data`, `Plugins`,
and `FWU` MTP storages. It does not expose Configuration, NVS, or raw flash.
On a clean virtual image, select EF800 Application's USB MTP personality:

1. Acquire keypad control.
2. From Main menu, press `e` for Settings, then `p` for Port assignment.
3. Select USB Port `4`, select device `m` (USB MTP), then choose the USB MTP
   connection in the connection list and press Enter.

The browser then lists folders, downloads files, and accepts drag-and-drop or
file-picker uploads into the current directory. The gateway rejects absolute or
traversal paths and limits each browser file to 32 KiB. Transfers use bounded
simulator messages, the in-process Zephyr USB host, the native-sim virtual UHC
and UDC, and EF800 Application's production device-next MTP class. The browser
gateway does not read or write the simulator filesystem directly. Progress,
completion, failure, and cancellation are surfaced in the file panel; a
download is offered only after byte count and checksum verification.

This verifies behavioral native-sim MTP flow only. It is not physical USB, QSPI
qualification, or a request to flash a target.

## EF800 Application diagnostics

The browser front panel includes a bounded **EF800 Application logs** panel for the
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

## Native-sim plugins and virtual devices

Upload native-sim measurement `.PMP` files through the MTP file panel by
selecting the `Plugins` storage. The same 32 KiB browser transfer limit,
relative-path checks, virtual USB host, and production EF800 Application MTP class
apply. Plugin discovery is startup-only; upload does not hot-load code. Request
a simulator restart separately to make new packages available to normal startup
discovery.

Native-sim and physical EF800 use the same ELTA3/FTS3XX `user-worker` package
ABI and pointer-free mediated services. Native-sim executes callbacks on a
serialized worker thread and checks the worker session/capability protocol, but
has no hardware MPU/userspace isolation. Physical EF800 retains the unprivileged
worker and external-SRAM-backed executable/rodata arenas when external SRAM is
validated.

The gateway keeps `POST /api/simulator/restart` for scripted activation and
`POST /api/devices/<1|2>` for bounded virtual serial profiles. The legacy
`POST /api/plugins` package-upload endpoint remains for API compatibility, but
the browser no longer has a separate plugin-management panel. Only native-sim
packages are accepted; physical ARM packages remain separate artifacts.

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
