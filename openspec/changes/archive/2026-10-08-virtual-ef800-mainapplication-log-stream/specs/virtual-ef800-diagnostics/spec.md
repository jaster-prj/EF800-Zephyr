## ADDED Requirements

### Requirement: Publish bounded native-sim EF800 Application log records
The Virtual EF800 native-sim target SHALL publish formatted EF800 Application log
records through the existing private front-panel protocol without blocking the
logging producer on browser or socket availability. The log stream SHALL be
native-sim-only and SHALL NOT alter physical EF800 console routing.

#### Scenario: EF800 Application emits a log record
- **WHEN** a EF800 Application module emits an enabled log record
- **THEN** the native simulator enqueues a bounded record containing a sequence,
  severity, and bounded diagnostic text for front-panel delivery

#### Scenario: No browser is connected
- **WHEN** EF800 Application emits logs while the front-panel client is absent
- **THEN** EF800 Application continues without waiting for a client and the
  simulator retains only the configured bounded recent history

#### Scenario: Log producer exceeds the queue bound
- **WHEN** log production exceeds the native-sim queue capacity
- **THEN** older records are evicted or records are reported as dropped according
  to the documented bounded policy, without unbounded memory growth or producer
  blocking

### Requirement: Replay bounded recent history and continue live delivery
The native simulator SHALL retain a bounded recent history of log records and
SHALL send that history in sequence order to each newly connected front-panel
client before sending subsequent live records. Records SHALL carry monotonic
sequence numbers so clients can identify gaps or discontinuities.

#### Scenario: Gateway connects after startup logs
- **WHEN** the gateway establishes the front-panel connection after EF800 Application
  has emitted log records
- **THEN** the simulator sends the retained history in oldest-to-newest order,
  marks replayed records, and then delivers new live records

#### Scenario: History reaches its configured bound
- **WHEN** more records exist than the configured history count or byte limit
- **THEN** only the newest records within both bounds are replayed and the client
  can observe that earlier sequence numbers were not retained

#### Scenario: Client reconnects after a disconnect
- **WHEN** the gateway reconnects after a front-panel connection loss
- **THEN** the simulator provides the current bounded history and preserves the
  monotonic sequence space rather than restarting sequence numbering

### Requirement: Keep diagnostics transport bounded and isolated from control traffic
The front-panel protocol SHALL bound log record size, queue storage, and browser
fan-out. Log transmission SHALL NOT prevent keypad release, LCD updates, plugin
acknowledgements, profile control, or supervised simulator restart from being
processed.

#### Scenario: A large log burst occurs
- **WHEN** the simulator produces a burst of records while control or display
  traffic is active
- **THEN** log delivery is bounded and lower priority than required control
  acknowledgements and the simulator remains responsive

#### Scenario: Log transport fails
- **WHEN** sending a log record fails because the client disconnects or the socket
  rejects the frame
- **THEN** the simulator abandons that client safely, releases held virtual keys,
  and continues running or reconnecting without recursive logging failure

#### Scenario: Malformed log message reaches the gateway
- **WHEN** the gateway receives a log frame with an invalid length, severity, or
  payload encoding
- **THEN** it rejects or ignores that record without corrupting subsequent protocol
  frames or the browser connection

### Requirement: Present native-sim diagnostics in the browser
The web gateway SHALL translate valid simulator log records into bounded browser
WebSocket messages, and the browser front panel SHALL provide a rolling diagnostics
view with recent history, live records, severity filtering, clear/pause controls,
auto-scroll behavior, and visible dropped-record or discontinuity state.

#### Scenario: Browser opens the front panel
- **WHEN** a browser connects to the gateway WebSocket
- **THEN** it receives retained log history followed by live records and displays
  replayed records distinctly from newly arriving records

#### Scenario: Browser pauses log rendering
- **WHEN** the user pauses the diagnostics view while logs continue arriving
- **THEN** simulator delivery remains unaffected and the browser applies its own
  bounded pending-render policy without unbounded DOM growth

#### Scenario: Browser clears diagnostics
- **WHEN** the user clears the diagnostics view
- **THEN** only the browser's visible and pending display state is cleared; the
  simulator log history and sequence counter remain unchanged

#### Scenario: Browser observes a sequence gap
- **WHEN** the browser receives a later sequence number after one or more missing
  records
- **THEN** it displays a bounded dropped/discontinuity indication instead of
  presenting the records as a contiguous complete history

### Requirement: Identify diagnostics as behavioral evidence
The Virtual EF800 diagnostics stream SHALL be documented and presented as
native-sim behavioral evidence and SHALL NOT claim qualification of physical
console routing, STM32 behavior, ARM executable loading, MPU containment,
electrical timing, or fitted peripheral wiring.

#### Scenario: Native-sim diagnostic stream is used during debugging
- **WHEN** a developer reviews EF800 Application logs through the browser
- **THEN** the deployment identifies the records as virtual behavioral diagnostics
  and retains the existing hardware qualification boundary
