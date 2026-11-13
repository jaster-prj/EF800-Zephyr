# EF800Application Foundation Status

Initialized by this increment:

- Zephyr C entry point and existing keyboard host parser/UART processing.
- Bounded keyboard startup wait with key and event logging.
- Zephyr watchdog task when the `watchdog0` device is ready.
- Status LED through the `status-led` devicetree alias.
- Minimal I2C probe for MCP23017 at `0x24` (7-bit address; `0x48` on the wire).
- Optional standard Zephyr DS3231 RTC API probe when a supported
  `maxim,ds3231-rtc` node is present.
- Optional QSPI FAT mount, disabled by default, with no format-on-failure and
  no USB MSC sharing.
- Complete T6963 text/graphics command surface used by the legacy helper:
  pixel read-modify-write, text/graphic/character-generator clear, graphic
  cursor, custom character, display mode, cursor pattern/pointer, line, and
  circle operations.
- Local-time RTC service with Gregorian validation, signed UTC offset in
  minutes, daylight-saving flag, UTC conversion, DS3231 write, Zephyr realtime
  clock synchronization, and persisted timezone settings when storage is
  enabled.
- Keyboard RTC editor: press `z`, enter weekday, date, month, year, hour,
  minute, second, UTC offset as hours or `HH:MM`, and DST (`0` or `1`); the
  final Enter writes the RTC.
- Backend-neutral persistence and Workspace services with safe path handling,
  native fake storage, Workspace create/select/restore/list, and append.
- EF800Application Workspace menu and Position entry dialog. Position records use
  fixed-point millimetres and append through the selected Workspace.
- Five logical port descriptors with active connection validation and
  versioned/checksummed port-assignment persistence.
- Fixed Port 5 BlueNRG assignment, persisted connection modes, UART ownership,
  metadata reconciliation, console fallback, and bounded assignment UI.
- Elta3 and FTS3XX wire parsers, command framing, fixed-point measurement
  records, Workspace append, assigned-UART serial configuration, and the
  EF800Application measurement workflow.
- EF800Application Settings port-assignment UI with connection-mode selection,
  fixed-port protection, removal, and storage errors shown without blocking the
  main loop.
- The routing increment, runtime status snapshot, legacy-style display header,
  and controlled EF800Application hardware path are verified in the current
  working tree. See `docs/migration-status.md` for the remaining hardware and
  QSPI/USB ownership gates.
- Shell commands on the console UART:
  - `rtc get`
  - `rtc set YYYY-MM-DDTHH:MM:SS offset_hours_or_HH:MM dst`

Still not ported:

- Elta3/FTS3XX hardware protocol validation and device-specific configuration
  persistence.
- Workspace/Position operation against the physical QSPI/FAT volume.
- Production validation of the fitted QSPI part and existing FAT contents.
- DS3231 alarm/update workflows and board-specific alarm/interrupt wiring.
- Hardware validation of T6963 bus timing, pixel bit order, and control-line
  polarity remains open. The current target run proves controller readiness,
  not complete electrical qualification.
