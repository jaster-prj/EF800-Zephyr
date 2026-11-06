# Operations

This page is an architecture-documentation entry point, not authorization to
program hardware. Follow the repository's hardware and development procedures
before interacting with a target.

## Physical target safety

- Do not flash without explicit authorization, a confirmed MCU/probe pairing,
  and the target power/SWD checks in [`../../AGENTS.md`](../../AGENTS.md).
- Never flash an image built for the L496 onto the L431 or vice versa.
- The development Main image can destroy a recognized legacy QSPI FAT volume.
  Do not boot it if any old data must be retained.
- Keep QSPI FAT and MSC ownership mutually exclusive.
- Treat QSPI geometry, update power-loss behavior, USB interoperability, and
  fitted peripheral behavior as hardware qualification gates.

## Virtual EF800

Use the procedures in [`../../docs/virtual-ef800.md`](../../docs/virtual-ef800.md)
and [`../../GSE/Virtual-EF800/README.md`](../../GSE/Virtual-EF800/README.md).
Its TCP control port is private to the Compose network; the browser connects to
the gateway. Simulator flash is disposable by default. Never point persistent
simulator storage at a physical flash image.

## Diagnostics and qualification

Use [`../../docs/testing.md`](../../docs/testing.md) for test scopes and
[`../../docs/qspi-migration.md`](../../docs/qspi-migration.md) plus
[`../../docs/flash-layout.md`](../../docs/flash-layout.md) for hardware gates. A
passing native suite is behavioral evidence only.
