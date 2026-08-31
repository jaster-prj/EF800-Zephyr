# EF800 keyboard protocol

The L496 host and independent L431 keyboard controller communicate over UART:

```text
9600 baud, 8 data bits, odd parity, 2 stop bits
```

The controller sends `0xAA` after startup. Commands are single-byte commands,
except `0xF3`, `0xF6`, and `0xEF`, which are followed by command data. Command
responses start with `0xA0` and contain the command byte and either `0x7F`
(`ACK`) or `0xFE` (`RESEND`).

Keyboard data uses PS/2-style make/break sequences:

```text
optional 0xE0/0xE1 prefix, optional 0xF0 release prefix, scancode
```

The byte values and translated event map are shared in
`shared/keyboard_protocol/` and are based on
`mbedos/mbed-uart-keyboard` plus the controller source.

## RTC entry workflow

The MainApplication keyboard editor is opened with `z`. It collects these
fields in order and commits them on the final Enter:

```text
weekday 1..7
date 1..31
month 1..12
year 2000..2099
hour 0..23
minute 0..59
second 0..59
UTC offset as signed hours or `+HH:MM`/`-HH:MM`
DST 0 or 1
```

The entered value is local civil time. The application converts it to UTC,
writes UTC to the DS3231, and updates Zephyr's realtime clock. The offset and
DST flag are stored as `/.configuration/time_settings` when the FAT service is
enabled.

The shell equivalent is:

```text
rtc get
rtc set YYYY-MM-DDTHH:MM:SS offset_hours_or_HH:MM dst
```
