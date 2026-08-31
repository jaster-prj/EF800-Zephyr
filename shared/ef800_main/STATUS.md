# EF800 MainApplication Foundation Status

Initialized by this increment:

- Zephyr C entry point and existing keyboard host parser/UART processing.
- Bounded keyboard startup wait with key and event logging.
- Zephyr watchdog task when the `watchdog0` device is ready.
- Status LED through the `status-led` devicetree alias.
- Minimal I2C probe for MCP23017 at `0x48`.
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
- Shell commands on the console UART:
  - `rtc get`
  - `rtc set YYYY-MM-DDTHH:MM:SS offset_hours_or_HH:MM dst`

Still not ported:

- Legacy Mbed C++ menu, Elta3/FTS3XX device protocols, port assignment,
  workspace persistence, and USB application behavior.
- Production validation of the fitted QSPI part and existing FAT contents.
- DS3231 alarm/update workflows and board-specific alarm/interrupt wiring.
- Hardware validation of T6963 bus timing, pixel bit order, and control-line
  polarity.
