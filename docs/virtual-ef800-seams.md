# Virtual EF800 Behavior Seams

This document records the first extraction boundary for the virtual EF800. It
is a behavioral seam, not an MCU or electrical abstraction.

## Ownership Map

| Behavior | Current owner | Reusable seam | Physical adapter | Virtual adapter |
|---|---|---|---|---|
| Menu selection and parent navigation | `application/ef800-application/src/main.c` | `shared/ef800_ui/ef800_ui.c` | LCD/T6963 rendering callback | Framebuffer rendering callback |
| Raw keyboard parsing and translation | `application/ef800-application/src/keyboard_host.c` | `struct ef800_keyboard_host` | LPUART1 ISR/transport | In-process byte producer |
| Plugin logical session | `shared/ef800_plugin/ef800_plugin_session.c` | `struct ef800_measurement_plugin_session` | EF800Application plugin host | Native virtual plugin host |
| Plugin display model | `shared/ef800_plugin/ef800_plugin_display.c` | bounded screen model | T6963 text/graphics calls | 240 x 64 framebuffer |
| Persistence and workspace | `shared/ef800_persistence/*` | backend interface | NVS/FAT/QSPI backend | fake or simulated-flash backend |
| Port ownership and routing | `shared/ef800_ports/*` | assignment/UART registry | STM32 UART devices | scripted virtual serial peers |
| RTC and battery snapshots | `shared/ef800_runtime/*` | runtime snapshot | DS3231/MAX17043 | deterministic clock/battery source |

The menu seam intentionally owns only state transitions and rendering
notification. Hardware access, workspace operations, plugin lifecycle, and
global navigation remain host responsibilities, matching the existing package
ownership and isolation decisions.
