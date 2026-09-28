# EF800 keyboard protocol

The L496 host and independent L431 keyboard controller communicate over UART:

```text
115200 baud, 8 data bits, no parity, 1 stop bit
```

The controller sends `0xAA` after startup. Commands are single-byte commands,
except `0xF3`, `0xF6`, and `0xEF`, which are followed by command data. Command
responses start with `0xA0` and contain the command byte and either `0x7F`
(`ACK`) or `0xFE` (`RESEND`).

Keyboard data uses PS/2-style make/break sequences:

```text
optional 0xE0/0xE1 prefix, optional 0xF0 release prefix, scancode
```

The firmware version request is `0xF1`. The response contains a one-byte
SemVer component for major, minor, and patch:

```text
F1                 request
A0 F1 major minor patch
```

The current keyboard application version is `1.1.1`, so the response is:

```text
A0 F1 01 01 01
```

The current EF800 compatibility map uses translated Set-2-style make values
with the existing EF800 `0xE0 0xF0 <code>` release framing. The navigation keys
are:

```text
Left   make E0 4B   break E0 F0 4B
Up     make E0 48   break E0 F0 48
Down   make E0 50   break E0 F0 50
Right  make E0 4D   break E0 F0 4D
```

These values are an EF800 compatibility contract. They are not a declaration
that every current key uses native PS/2 Set 2 make values; ordinary legacy
character mappings remain preserved from the Mbed controller protocol.

The byte values and translated event map are shared in
`shared/keyboard_protocol/` and are based on
`mbedos/mbed-uart-keyboard` plus the controller source.

## Debug virtual keyboard

MainApplication provides a shell interface for hardware interaction tests. It
is available on the MainApplication console, not on the keyboard-controller
UART:

```text
keyboard debug start
keyboard debug press <key-name>
keyboard debug release <key-name>
keyboard debug stop
```

The keyboard controller owns the key-name-to-matrix-to-scancode mapping.
MainApplication sends the requested name without interpreting its matrix key
or scancode. Names are case-insensitive. Common names are:

```text
a ... z       0 ... 9
space         tab       enter       escape
backspace     delete    insert
shift         ctrl      alt         caps
on            off       up          down       left       right
comma         period    minus       plus
```

The aliases `return`, `esc`, `back`, and `control` are also accepted. A press
is persistent: it sends one make sequence and remains held until its matching
release or until debug mode is stopped. Stopping debug mode releases every
virtual key still held before normal matrix scanning resumes.

Internally, a debug key request uses `0xEF`, an action byte, a name length, and
the name bytes:

```text
0xEF action name_length name_bytes
```

`action` is `0x01` for press and `0x02` for release. A valid request produces
only the ordinary PS/2-like make or break bytes. It does not produce a new
command response. Unknown names, invalid names, duplicate presses, unmatched
releases, and incomplete requests are ignored without UART output.

The debug session disables physical matrix scanning so physical and virtual
events cannot be interleaved. The bootloader and BackupApplication do not
consume this protocol or process keyboard-controller UART input.

See `docs/hardware-debug-keyboard.md` for the physical target procedure.

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
