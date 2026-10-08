## 1. Protocol Contract and Bounds

- [x] 1.1 Define the native-sim log-record message type, payload layout, severity values, replay/live flags, sequence semantics, and explicit record/history/queue bounds.
- [x] 1.2 Implement protocol encode/decode helpers for bounded log records with validation for length, severity, flags, sequence, and text encoding.
- [x] 1.3 Add protocol tests for round trips, fragmented frames, multiple records, maximum bounds, malformed payloads, and sequence/discontinuity metadata.

## 2. Native-Sim Log Production and History

- [x] 2.1 Add a native-sim-only diagnostic sink beside the existing EF800 console log backend without changing physical target routing.
- [x] 2.2 Implement the bounded producer queue and recent-history ring with synchronized access, monotonic sequence numbers, explicit eviction, and dropped-record state.
- [x] 2.3 Preserve severity and bounded formatted text at the diagnostic sink while preventing transport-originated failures from recursively re-entering the browser log stream.
- [x] 2.4 Integrate front-panel worker draining so it is the sole sender of log frames and prioritizes keypad/control, LCD, plugin, and restart traffic over diagnostics.
- [x] 2.5 Replay retained records in chronological order after initial simulator state and before subsequent live records, including replay/discontinuity metadata.
- [x] 2.6 Add native-sim tests for no-client logging, history bounds, queue overflow, replay order, reconnect sequence continuity, client disconnect, and control responsiveness during log bursts.

## 3. Gateway Translation and Fan-Out

- [x] 3.1 Extend the Python protocol constants/parser for native log records and validate malformed records without corrupting the connection.
- [x] 3.2 Translate valid simulator log records into bounded browser WebSocket messages while preserving sequence, severity, replay, truncation, and gap information.
- [x] 3.3 Keep gateway fan-out bounded so a slow browser cannot block simulator reads or other viewers, and handle simulator reconnects as a new diagnostic epoch.
- [x] 3.4 Add gateway tests for live delivery, history replay, malformed records, sequence gaps, reconnect behavior, multiple viewers, and bounded slow-client handling.

## 4. Browser Diagnostics Panel

- [x] 4.1 Add a clearly labeled native-sim behavioral diagnostics panel with bounded visible history, severity filtering, replay markers, and discontinuity/dropped-record indicators.
- [x] 4.2 Add pause/resume, clear-visible-history, and auto-scroll controls whose browser-local state does not alter simulator history or sequence counters.
- [x] 4.3 Bound paused and rendered browser records to prevent unbounded DOM or JavaScript memory growth while keeping simulator delivery independent of rendering state.
- [x] 4.4 Add web-asset tests for the diagnostics panel, controls, message handling, bounded rendering, severity filtering, and behavioral-evidence labeling.

## 5. Documentation and Integration Verification

- [x] 5.1 Document the browser diagnostics stream, history/reconnect semantics, bounds, and behavioral-evidence limitation in the Virtual EF800 documentation.
- [x] 5.2 Add or update Compose/integration coverage proving the existing private simulator port and loopback-only web deployment remain unchanged.
- [x] 5.3 Run the Virtual EF800 Python tests, native-sim behavioral tests, and Compose integration tests with the diagnostics feature enabled.
- [x] 5.4 Run the required project verification suites and record that physical console behavior and the hardware qualification boundary remain unaffected.
