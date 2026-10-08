## Context

The Virtual EF800 already runs the production MainApplication composition root in
Zephyr `native_sim`. Its native front-panel service owns one bounded TCP connection
to the Python gateway, which translates LCD frames, keypad operations, plugin
status, and virtual-device controls to a browser WebSocket. The browser currently
has no path for MainApplication logs.

MainApplication logging is formatted by the shared EF800 log backend and written
through the selected EF800 console UART. On native_sim, the default console path is
the emulated/native-PTY UART rather than the browser transport. The existing log
backend has a small formatting buffer, but it has no browser-facing queue or
history. The front-panel worker also performs synchronous socket writes, so logging
must not write directly to that socket.

The design must preserve the accepted native-sim and web-gateway boundaries:
native_sim remains behavioral evidence, the gateway remains the sole simulator
client, the raw simulator endpoint remains private, and physical console behavior
is unchanged.

## Goals / Non-Goals

**Goals:**

- Publish bounded native-sim MainApplication records through the existing simulator
  protocol and gateway WebSocket.
- Retain a small recent history, recommended at 64 records and approximately 8 KiB
  of text, subject to implementation constants and tests.
- Replay history after connection before continuing live delivery, with monotonic
  sequence numbers and explicit replay/discontinuity metadata.
- Keep log production non-blocking and isolate log bursts from keypad, LCD, plugin,
  profile, and restart control traffic.
- Give the browser a bounded rolling diagnostics view with severity filtering,
  pause/clear, auto-scroll, and dropped-record visibility.
- Keep the feature native-sim-only and clearly behavioral.

**Non-Goals:**

- Changing physical EF800 console selection, UART routing, MCUmgr, shell, or USB
  debug behavior.
- Providing durable log storage across simulator restarts or flash volumes.
- Guaranteeing delivery of every log record across a connection failure.
- Exposing the simulator TCP endpoint or adding remote authentication.
- Adding arbitrary browser-to-device commands through the diagnostics channel.
- Replacing the existing console/PTTY path for headless native-sim debugging.

## Decisions

### Use a native-sim diagnostic sink alongside the existing console backend

The native-sim build will fan log records into a bounded diagnostic queue while
retaining the existing EF800 console output path. The physical build path will not
enable this sink. This keeps the logging feature additive and prevents browser
connectivity from becoming a prerequisite for normal application diagnostics.

Alternatives considered:

- Redirect all native-sim logs to process stdout: rejected because it loses the
  existing structured transport boundary and makes reliable browser delivery and
  severity handling harder.
- Make the gateway open a second simulator connection: rejected because ADR-0004
  establishes one gateway-owned simulator connection and the simulator currently
  has a single-client front-panel boundary.
- Send logs directly from the log backend to the socket: rejected because logging
  can run on multiple threads and socket failures could block or recursively log.

### Use a bounded producer/consumer queue and recent ring buffer

The logging producer will enqueue a bounded record without waiting on the socket.
The native front-panel worker will drain queued records and own all protocol writes.
A separate bounded ring will retain recent records for replay. The recommended
default is 64 records and about 8 KiB of text, with per-record text capped near 120
bytes. The implementation will define one explicit eviction policy and expose a
dropped/discontinuity marker when records cannot be retained or delivered.

The queue and history will be native-sim-only. If the client is absent, the
simulator continues running; history is retained only within the configured bounds.

### Add a versioned log message to the existing binary protocol

The protocol will add a message type for a bounded log record. Its payload will
include a monotonic sequence, severity, replay/live flags, and bounded text. The
wire payload will stay below the existing protocol maximum. The gateway will parse
and validate the binary record before translating it to a browser JSON message.

The protocol will preserve sequence gaps rather than fabricating missing records.
The browser will use those gaps and explicit flags to show discontinuity. Log
records will not be acknowledged individually; the connection remains a streaming
diagnostic channel, not a durable delivery protocol.

### Replay history after simulator state initialization

When a front-panel client is accepted, the simulator will send its existing
capabilities and current LCD/plugin state, then the bounded history in chronological
order, and then queued/live records. The exact ordering will be tested and documented
so a browser can initialize without mistaking replayed records for new events.

The sequence counter is process-lifetime state. It may restart after a simulator
restart; the browser will treat the connection/restart status as a new diagnostic
epoch rather than requiring persistence in the virtual FAT volume.

### Keep gateway and browser history separate

The gateway will retain only the simulator-provided records needed to broadcast
current state; the browser will own its visible rolling list, pause queue, filters,
and clear operation. Browser clear therefore cannot erase simulator history or alter
the native sequence counter. Gateway fan-out will remain bounded per client so a
slow browser cannot block simulator reads or other viewers.

### Avoid recursive transport diagnostics

Socket-send failures from the front-panel worker must not re-enter the browser log
queue indefinitely. Transport failures will be handled as connection state and
bounded internal diagnostics, with any transport-originated logging either suppressed
from the browser stream or marked so it cannot recursively trigger another send.

## Risks / Trade-offs

- [Log bursts could compete with LCD/control traffic] -> Use a bounded queue,
  prioritize control and display messages, and test restart/key-release latency.
- [A bounded history can omit important startup records] -> Replay the newest
  records in sequence order and display explicit gaps/dropped state rather than
  implying complete history.
- [Formatting may lose structured module/severity information] -> Preserve severity
  and sequence at the native sink; retain formatted text as the bounded payload and
  avoid duplicating Zephyr formatter internals unless required by the API.
- [Concurrent logging may expose races] -> Keep queue mutation synchronized with a
  native-sim-safe bounded primitive and make the socket worker the sole sender.
- [Browser log rendering can grow the DOM] -> Cap visible and paused records,
  discard or summarize old browser entries, and test long-running sessions.
- [Diagnostics may be mistaken for hardware qualification] -> Label the panel and
  documentation as native-sim behavioral evidence and preserve the existing gates.
- [A simulator restart invalidates sequence continuity] -> Broadcast connection and
  restart state, and treat a new process as a new diagnostic epoch.

## Migration Plan

1. Define the native-sim log record contract, bounds, flags, sequence semantics,
   and protocol constants.
2. Add the native-sim producer queue/history and front-panel worker drain path while
   preserving the existing console backend and physical builds.
3. Extend protocol parsing and gateway translation, including reconnect and
   malformed-frame behavior.
4. Add the browser diagnostics panel and bounded rendering controls.
5. Add unit, protocol, gateway, native-sim, web-asset, and Compose coverage for
   replay, live delivery, overflow, disconnect, and control responsiveness.
6. Update Virtual EF800 documentation and verify the normal native test and Compose
   deployment suites.

Rollback consists of disabling the native-sim log message production and removing
   the gateway/browser handling. Existing LCD, keypad, plugin, and virtual-device
   messages remain unchanged; physical console behavior is unaffected throughout.

## Open Questions

- Whether the exact history bound should be 64 records/8 KiB or another value after
  measuring native-sim memory and startup log volume.
- Whether the wire payload should carry a structured module name or only the already
  formatted text plus severity; the latter is the fallback if Zephyr log metadata
  cannot be exposed cleanly at the sink.
- Whether browser severity filtering should happen only in the browser or whether
  the gateway should support a negotiated filter to reduce WebSocket traffic.
