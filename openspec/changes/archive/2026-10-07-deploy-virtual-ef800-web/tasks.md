## 1. Simulator Protocol Foundation

- [x] 1.1 Extract the binary framing constants, encoder, incremental parser, LCD payload decoder, and protocol exceptions from `virtual_frontpanel.py` into a widget-independent Python module under `GSE/Virtual-EF800`.
- [x] 1.2 Update `scenario_client.py` to consume the reusable protocol module while preserving direct headless connection and command-line behavior.
- [x] 1.3 Migrate and expand protocol tests for fragmented and combined frames, invalid magic/version/length, payload bounds, capabilities, acknowledgements, LCD sequence/pixels/text, and key message encoding.

## 2. Web Gateway

- [x] 2.1 Add a pinned Python dependency definition and an `aiohttp` web-service entry point configurable with simulator host/port and HTTP bind address/port.
- [x] 2.2 Implement the single simulator connection manager with incremental binary parsing, retained complete LCD state, bounded exponential reconnect, stale-state marking, and clean cancellation.
- [x] 2.3 Define and validate a versioned, bounded browser WebSocket message contract for status, LCD state, controller ownership, and semantic keypad operations.
- [x] 2.4 Implement WebSocket viewer registration and latest-state delivery without accumulating unbounded per-viewer frame queues.
- [x] 2.5 Implement explicit exclusive controller acquisition/release, reject non-controller input, and broadcast ownership changes to all viewers.
- [x] 2.6 Track controller-held keys and send release-all on lease release, WebSocket loss, simulator loss, gateway shutdown, and every uncertain-control path.
- [x] 2.7 Add HTTP readiness/status output that distinguishes web availability from simulator connectivity without opening an extra simulator connection.
- [x] 2.8 Add asynchronous gateway tests with a fake simulator and multiple WebSocket clients covering either startup order, reconnection, cached/stale state, control contention, malformed messages, slow viewers, and release-all cleanup.

## 3. Browser Front Panel

- [x] 3.1 Add dependency-free HTML, CSS, and JavaScript assets that connect to the gateway, display simulator and controller status, and reconnect without replaying stale input.
- [x] 3.2 Render complete 240 x 64 monochrome LCD frames on a scaled canvas and display the available logical text state without accepting partial frames.
- [x] 3.3 Add visible viewer, acquire-control, and release-control states and disable keypad injection unless the browser owns control.
- [x] 3.4 Add the EF800 semantic keypad with distinct press/release behavior for mouse, touch, and documented host-keyboard mappings, including browser blur/unload release handling.
- [x] 3.5 Add browser-side unit or headless-browser tests for framebuffer decoding/rendering, connection status, control ownership, key hold/release, malformed messages, and reconnect behavior.

## 4. Dedicated Simulator Image

- [x] 4.1 Inspect the current `native_sim` executable with `file` and `ldd` inside the Zephyr environment and record the required architecture and dynamic runtime libraries in the image implementation or build documentation.
- [x] 4.2 Select and pin an immutable compatible Zephyr builder image and runtime base, retaining the manifest-pinned Zephyr v4.4.1 workspace inputs.
- [x] 4.3 Add an Alpine simulator Dockerfile that copies only the Linux executable built by `EF800_Devcontainer` and its demonstrated runtime compatibility libraries.
- [x] 4.4 Configure the simulator executable as the non-root foreground container process with internal port `8800`, signal-safe shutdown, and no dependency on host `build/` output or `EF800_Devcontainer`.
- [x] 4.5 Add a clean-checkout image smoke test that starts the packaged executable and verifies Virtual EF800 initialization and front-panel listener availability on the intended runtime architecture.

## 5. Web Image and Compose Deployment

- [x] 5.1 Add a non-root web-service image containing the pinned Python runtime dependencies and browser assets, with simulator service DNS configuration supplied by environment.
- [x] 5.2 Add a Docker Compose application with separate simulator and web services on a private network, no published simulator port, and a configurable web port bound by default to `127.0.0.1`.
- [x] 5.3 Configure restart, shutdown, and HTTP health behavior without a TCP probe that can consume the simulator's single client connection.
- [x] 5.4 Add Compose acceptance checks for clean startup, either service starting first, independent service restart, gateway reconnection, graceful shutdown, loopback-only publication, and absence of host port `8800`.
- [x] 5.5 Verify multiple browser viewers, one controller, controller disconnect with a held key, simulator restart, fresh control acquisition, and continued direct headless operation when the gateway is absent.

## 6. Migration, Documentation, and Closeout

- [x] 6.1 Update `GSE/Virtual-EF800/README.md`, `docs/virtual-ef800.md`, and `docs/testing.md` with Compose build/start/stop/rebuild commands, localhost URL, control ownership, health interpretation, headless use, and troubleshooting.
- [x] 6.2 Remove the Tkinter front-panel entry point and its obsolete launch instructions only after browser feature parity is covered, retaining migrated protocol tests and the headless scenario client.
- [x] 6.3 Extend CI to run Python gateway/protocol tests, browser tests, simulator and web image builds, runtime smoke tests, and Compose acceptance checks with useful failure logs.
- [ ] 6.4 Run the complete native Twister collection in `EF800_Devcontainer` and confirm the documented baseline remains passing.
- [ ] 6.5 Pristine-build the bootloader, backup, main, keyboard, and virtual targets in `EF800_Devcontainer`, then run `scripts/verify-images.sh` for the physical firmware images.
- [x] 6.6 Update `docs/migration-status.md` with the deployed web-front-panel capability, resulting test baseline, and unchanged behavioral-versus-hardware qualification boundary.
