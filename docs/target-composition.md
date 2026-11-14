# EF800 Target Composition Profile

The target composition mechanism stores bounded COMP-Nr. identifiers in the
private EF800 configuration area. It provides persistence, compatibility, and
restart-scoped activation only. The catalog includes target-specific USB MSD
selections and the LCD contrast mode group. `LCDCONTR0` is the default and keeps
static contrast; `LCDCONTR1` enables the optional PB8 contrast PWM after restart.

Concrete COMP-Nr. meanings and consumers require later specified changes.

## Logical Record

The profile has this canonical LF-terminated form:

```text
format_version=1
product=EF800
generation=1
composition=
crc32=XXXXXXXX
```

`composition=` occurs exactly once. Its value is a space-separated list:

```text
composition=USBMSD1 LCDCONTR1
```

`USBMSD0`, `USBMSD1`, `LCDCONTR0`, and `LCDCONTR1` are registered hardware
selections. Unknown tokens remain opaque and are not activated.

Input may use repeated ASCII spaces around tokens and CR/LF line endings.
Canonical output uses one space between tokens and LF endings. The CRC-32 is
calculated over the canonical lines before `crc32=`. Duplicate or missing
fields, duplicate tokens, unsupported versions, unknown fields, invalid CRCs,
and partial records reject the complete candidate.

## Bounds

- Token syntax: an uppercase ASCII letter followed by uppercase letters or
  digits.
- Maximum token length: 16 characters.
- Maximum token count: 32.
- Maximum encoded record size: 1024 bytes.
- Product identifier: `EF800`.

The parser uses fixed-capacity storage and never partially activates an invalid
record.

## Recoverable File Slots

One logical profile is stored as two private configuration files:

```text
/EF800:/.configuration/target_profile_a
/EF800:/.configuration/target_profile_b
```

At startup the service validates both files and selects the newest complete
valid generation using unsigned serial-number ordering. A valid successor can
wrap from generation `4294967295` to `0`. Equal generations with different
contents, or an exact half-range generation difference, are conflicts and cause
default behavior rather than a nondeterministic selection.

A commit writes the inactive or older slot, reads it back, decodes it, and
compares it with the staged profile. A failed or interrupted write therefore
leaves the previous valid slot available.

## Catalog, Defaults, and Unknown Tokens

The repository catalog maps known tokens to groups and declares at most one
default per group. Resolution selects a recognized stored token for a group or
the registered default when that group is omitted.

Syntactically valid tokens absent from the current catalog are:

- retained in the stored snapshot;
- reported as unknown;
- preserved by service-managed add and remove operations;
- excluded from the effective composition;
- never interpreted from their spelling or prefix.

The physical EF800 Application supports `USBMSD0` (Main MSC disabled) and
`USBMSD1` (Main MSC enabled), with `USBMSD1` as the default. Select the mode on
the EF800 Application shell with `usbmsd mode USBMSD0` or `usbmsd mode USBMSD1`;
the selection requires restart. Native-sim defaults to `USBMSD0` and does not
compile Main MSC. The `LCDCONTR` group defaults to `LCDCONTR0`; EF800 Application
and EF800 Backup resolve its mode at startup, while contrast duty remains
a separate runtime setting.

## Modification and Activation

The shared composition service exposes immutable active and optional pending
snapshots. Callers can stage:

- exact-token addition;
- exact-token removal;
- complete bounded replacement;
- discard of all pending changes.

Staging validates syntax, duplicates, current catalog conflicts, and encoded
size without changing the active snapshot. A successful commit increments the
generation and verifies the new slot, but still does not activate it. The
service reports restart-required state, and the committed profile becomes
active only when the service initializes during the next boot.

The profile mechanism provides no generic Settings menu, MTP root, or browser
operation. The LCD contrast feature provides a narrow `lcdcontrast mode`
console command that stages and commits the LCDCONTR group selection through
the shared service. Its effect remains restart-required; the command does not
change the active PWM state in the current boot.

## Failure Behavior

Missing files, unavailable configuration storage, two invalid slots, or a slot
conflict do not prevent EF800 Application startup. The service reports the
condition and resolves the repository defaults. It does not create a default
file automatically, format FAT, modify internal boot metadata, or reconfigure
running services.

Physical QSPI/FAT availability, media geometry, power-loss behavior, and USB
ownership remain hardware qualification gates. Native tests prove parser,
resolver, service, and simulated persistence behavior only.
