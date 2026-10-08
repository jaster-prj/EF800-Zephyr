## Context

Virtual EF800 is a Zephyr `native_sim` application that listens on TCP port `8800` and permits one active front-panel client. The existing Windows Tkinter program combines binary protocol parsing, socket ownership, LCD rendering, and keypad widgets. Interactive startup therefore depends on the reusable `EF800_Devcontainer`, host Python/Tkinter, and manual Docker port publication.

The deployment must preserve ADR-0003: the simulator remains a dedicated `native_sim` behavioral target, shares production behavior, and cannot provide hardware qualification. The current binary protocol and headless scenario client remain useful acceptance surfaces. The first web deployment is trusted developer tooling bound to localhost; remote access, authentication, and TLS are outside this change.

## Goals / Non-Goals

**Goals:**

- Run the simulator in a dedicated container without using `EF800_Devcontainer` as its runtime.
- Start, stop, and rebuild the simulator and browser front panel as one Compose application.
- Replace Tkinter with a browser UI while preserving the simulator's bounded binary protocol and normal keyboard path.
- Keep raw simulator TCP traffic private and expose only a localhost HTTP endpoint.
- Permit multiple viewers but exactly zero or one active controller, with safe release behavior.
- Retain deterministic headless use and test protocol framing, gateway state, browser behavior, and packaged runtime startup.

**Non-Goals:**

- LAN or Internet access, authentication, authorization across users, TLS, or reverse-proxy deployment.
- Replacing the binary simulator protocol with WebSocket or embedding an HTTP server in Zephyr.
- Running more than one web gateway against one simulator instance.
- Replacing the existing headless scenario client or weakening the hardware qualification boundary.
- Making `EF800_Devcontainer` disposable or changing the normal firmware development workflow.
- Optimizing the first runtime image to the smallest possible distribution at the expense of native executable compatibility.

## Decisions

### Deploy two services with one private simulator connection

Docker Compose will define a `virtual-ef800` simulator service and a `virtual-ef800-web` gateway service. The simulator exposes port `8800` only to the Compose network. The gateway resolves the simulator by service name, owns the sole long-lived binary connection, and publishes its HTTP endpoint as `127.0.0.1:<configured-port>`.

The gateway will tolerate either startup order and reconnect with bounded exponential backoff. Compose startup ordering is advisory rather than correctness-critical. A TCP connect probe will not be used as simulator health because it can consume the one permitted client connection. The gateway will instead expose HTTP health/status that reports both web readiness and simulator connectivity.

Alternatives considered:

- Publish `8800` to Windows and let every browser connect through a TCP bridge: browsers cannot use raw TCP, it exposes an internal protocol, and concurrent clients conflict with the simulator's single-client design.
- Put HTTP/WebSocket support inside the Zephyr application: this couples presentation dependencies to behavioral firmware and duplicates concerns better handled by a host service.
- Combine simulator and web service in one container: simpler startup, but defeats independent lifecycle, obscures process health, and makes the runtime less composable.

### Build the simulator in EF800_Devcontainer and run only its binary in Alpine

The simulator executable will be built inside the reusable `EF800_Devcontainer` using the manifest-pinned Zephyr workspace and written to the mounted `build/ef800-virtual/zephyr/zephyr.exe` path. A separate Alpine runtime image will copy only that executable and the minimal compatibility libraries required by its inspected ELF dependencies. The runtime image contains no west workspace, compiler, source tree, or development container.

Implementation must inspect the produced executable's architecture and dynamic dependencies and prove the runtime choice with a container smoke test. The initial runtime may deliberately use a compatible, non-minimal base; shrinking it is secondary to reliable `native_sim` execution. No host `build/` artifact is an image input.

Alternatives considered:

- Build inside a multi-stage Docker image: rejected because it duplicates the persistent Zephyr workspace, downloads the manifest dependencies again, and makes the normal development container irrelevant to the requested workflow.
- Run the full persistent devcontainer as the simulator: preserves today's coupling and leaves port publication and lifecycle management unresolved.
- Use Alpine with no compatibility packages: rejected unless the inspected executable is statically linked; the runtime must include only the required ABI compatibility libraries.

### Use an asynchronous Python gateway with a small explicit browser protocol

The existing Python parser and encoder behavior will be extracted from Tkinter concerns into a reusable protocol module. An asynchronous Python web service will maintain the simulator TCP stream, serve static browser assets, and provide a WebSocket endpoint. `aiohttp` is the preferred implementation because one pinned dependency provides asynchronous TCP coordination, HTTP, static assets, and WebSocket support without a separate ASGI server. The browser will use dependency-free HTML, CSS, JavaScript, and a canvas renderer.

The browser protocol will be distinct from the simulator protocol and use bounded, validated messages. Server-to-browser state includes connection status, controller ownership, frame sequence, packed or browser-ready pixel state, and logical text rows when present. Browser-to-server messages include control acquisition/release and semantic key-down/key-up/release-all operations. Protocol versioning will make incompatible changes explicit.

Alternatives considered:

- FastAPI plus an ASGI server: capable, but introduces more deployment components than this small gateway requires.
- Flask with background threads: familiar but awkward for one TCP stream plus multiple live WebSockets and coordinated shutdown.
- Transmit the raw binary protocol directly over browser WebSocket: reduces translation but leaks simulator-specific framing into UI code and does not model controller ownership or web status cleanly.

### Make browsers viewers until they acquire an exclusive control lease

Every WebSocket receives retained display and status updates. A browser must explicitly acquire control before its key messages are accepted. At most one session owns the in-memory lease; other sessions remain viewers. The gateway tracks keys held by the controller and sends simulator `release-all` when the controller releases its lease, disconnects, the gateway shuts down, or control state becomes uncertain.

Loss of the simulator connection clears the lease and held-key state, marks the retained display stale, and requires a fresh acquisition after reconnect. This avoids replaying input across simulator lifetimes. Browser identity is connection-scoped; no cookie-based durable identity or forced takeover is included in this localhost-only increment.

Alternatives considered:

- Let all browsers inject keys: simultaneous holds make disconnect cleanup ambiguous and allow one viewer to disrupt another.
- Permit only one WebSocket: simple, but prevents passive viewers and makes browser refresh unnecessarily exclusive.
- Automatically grant the first browser control: convenient but surprising; explicit acquisition makes ownership visible and testable.

### Keep the scenario client as a direct compatibility client

The web gateway and headless scenario client will not connect simultaneously because the simulator intentionally supports one client. Automated scenarios can continue to start the simulator without the web service and connect directly. Protocol constants and framing logic should be shared where practical, while tests retain malformed and fragmented transport coverage so sharing does not hide compatibility defects.

The Tkinter application will be removed after browser parity and tests are established. Its protocol/parser tests will migrate to the reusable protocol and gateway test suites rather than being discarded.

## Risks / Trade-offs

- [A `native_sim` executable built in one distribution does not run in the runtime stage] -> Inspect architecture and `ldd` output, begin with a compatible base, and gate the image with a startup smoke test before attempting size reduction.
- [Docker builds repeatedly download a large Zephyr workspace] -> Structure builder layers around manifest inputs and document cache behavior; pin revisions rather than reusing host build artifacts.
- [A health check steals the simulator's only client slot] -> Probe gateway HTTP state only and never open a second simulator TCP connection for health.
- [A browser disappears while a key is held] -> Track controller-held keys server-side and issue release-all on every lease-loss and connection-loss path.
- [WebSocket backpressure lets slow viewers accumulate frames] -> Retain and replace the latest complete frame per client rather than queueing an unbounded frame history.
- [Gateway reconnect races with stale browser input] -> Clear leases and held state on simulator loss and require explicit reacquisition.
- [Localhost-only assumptions are weakened by configuration] -> Bind the default Compose port explicitly to `127.0.0.1`, document that remote exposure is unsupported, and test the rendered Compose port mapping.
- [Shared Python protocol code gives false confidence] -> Keep wire-level fixture tests for fragmentation, invalid headers, payload limits, and expected simulator messages.

## Migration Plan

1. Extract reusable Python simulator-protocol framing and migrate existing host tests while retaining the scenario client.
2. Add the asynchronous gateway connection manager, retained state, controller lease, health/status, and unit/integration tests.
3. Add the static browser LCD canvas and keypad with WebSocket status and exclusive-control behavior.
4. Add the simulator multi-stage image and prove its architecture and dynamic runtime dependencies with a container smoke test.
5. Add the web image and Compose definition with a private simulator endpoint and loopback-only browser publication.
6. Add Compose-level startup, reconnect, restart, shutdown, port-exposure, and release-all acceptance tests.
7. Update CI and operating documentation, then remove the Tkinter entry point after browser feature parity is demonstrated.

Rollback removes the Compose and web deployment artifacts and restores the documented Tkinter launch path. The simulator binary protocol and headless scenario client remain compatible throughout, so rollback does not require firmware or protocol migration.

## Open Questions

- Which immutable digest of the existing Zephyr workspace/toolchain image provides the most reliable cached builder base for Zephyr v4.4.1?
- Does the packaged `native_sim` executable require a 32-bit userspace runtime, and which runtime packages are demonstrated by `file`/`ldd` inspection in the builder image?
- Should the initial browser transmit packed framebuffer bytes or server-expanded pixels? Packed bytes reduce bandwidth; expanded state simplifies browser code but is larger. The implementation should choose based on a small protocol fixture and keep the WebSocket payload bounded.
