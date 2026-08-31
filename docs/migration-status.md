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

## Software verification

Verified with the reusable `EF800_Devcontainer`, Zephyr `v4.4.1`, and Zephyr
SDK `1.0.1`:

- Second-level bootloader builds at `0x08000000` (`37264` bytes).
- Backup application builds at `0x08010000` (`53500` bytes).
- MainApplication builds at `0x08030000` (`91116` bytes).
- Keyboard-controller application builds (`20348` bytes).
- Native keyboard, protocol, RTC, persistence, and Position suite passes all ten
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
- Final MainApplication port and external device protocol validation.
- Physical QSPI/FAT validation of the Workspace and Position services.
- Position input and persistence on hardware requires validated Workspace
  storage.
