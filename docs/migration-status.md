# Migration status

## Implemented in the initial Zephyr increment

- Containerized west workspace skeleton.
- EF800 L496 and EF800 keyboard L431 board definitions.
- Legacy internal flash partitions.
- Shared keyboard protocol constants and ASCII/event map.
- Zephyr keyboard-controller scanner and UART command implementation.
- Zephyr keyboard-host parser.
- Bootloader and backup application foundations.
- Native keyboard protocol tests.
- MainApplication linked to its fixed `0x08030000` partition.
- Compile-time and CI checks for the fixed image layout and image sizes.
- Backend-neutral persistence API with safe names, read/write/append/stat/list/
  remove operations, and a no-storage Zephyr adapter.
- Workspace create, select, restore-current, list, and append behavior with a
  deterministic native fake backend.
- MainApplication Workspace menu integration with restore, create, select, and
  list actions while storage remains disabled by default.
- Position record formatting and Workspace append behavior using fixed-point
  millimetres.
- MainApplication Position entry dialog gated on a selected Workspace.
- Five logical port descriptors with capability-filtered device assignment.
- Portable, versioned, checksummed port-assignment persistence using the
  backend-neutral storage API.
- MainApplication Settings port-assignment UI with assignment removal; device
  protocol startup remains disabled until those protocols are ported.
- Fixed 40-byte little-endian boot metadata encoding with CRC validation,
  persisted connection modes, and fixed Port 5 BlueNRG assignment.
- Five-port assignment validation for active connection modes, including UART
  ownership checks and MainApplication connection-mode selection UI.
- MainApplication startup reconciliation for metadata-backed assignments and
  console fallback; bootloader uses the shared metadata read/write contract.
- Elta3 and FTS3XX protocol modules with exact command framing, response
  parsing, fixed-point measurements, serial defaults, record formatting, and
  Workspace append support.
- MainApplication assigned Elta3/FTS3XX UART setup using the persisted
  connection mode and measurement workflow.
- MainApplication Measurement workflow for assigned Elta3/FTS3XX devices:
  trigger, code/staff-height entry, save, discard, and fixed-measurement
  repetition through Workspace.

## Current Checkpoint

The routing increment is implemented and verified in the current working tree,
but remains uncommitted alongside the earlier provisional changes. The
authoritative software topology is five logical ports: UART3 RS232, UART1 with
optional flow control, UART4 plus alternate Port 3 modes, USB CDC/MSD, and
fixed BlueNRG/SPI1. Exact alternate pin ownership still requires schematic
confirmation before hardware use.

## Software verification

Verified with the reusable `EF800_Devcontainer`, Zephyr `v4.4.1`, and Zephyr
SDK `1.0.1`:

- Second-level bootloader builds at `0x08000000` (`35360` bytes).
- Backup application builds at `0x08010000` (`53644` bytes).
- MainApplication builds at `0x08030000` (`108824` bytes).
- Keyboard-controller application builds (`20348` bytes).
- Native keyboard, protocol, RTC, persistence, Position, port-assignment, and
  boot-metadata, measurement protocol, and record suite passes all nineteen
  tests.

The backup application currently uses about 82 percent of its 64 KiB
partition. Treat additions to that recovery image as size-sensitive.

## Hardware validation still required

- External QSPI NOR part, capacity, erase geometry, and maximum frequency.
- Actual STM32L496 clock source and board oscillator wiring.
- QSPI pinctrl support for the selected Zephyr revision.
- MCP23017 wiring and interrupt/reset behavior.
- T6963 LCD electrical timing.
- USB switch polarity and USB VBUS behavior.
- Production image signing and update package policy.
- Final MainApplication port and external device protocol hardware validation.
- Confirmed hardware UART routing and flow-control pin validation for assigned
  ports.
- Physical QSPI/FAT validation of the Workspace and Position services.
- Position input and persistence on hardware requires validated Workspace
  storage.
