# EF800 Measurement Plugin SDK

This application-owned SDK builds and tests two standalone PluginManager
measurement packages. Each package owns its measurement submenu declaration,
logical keyboard handling, measurement state, and bounded display model:

- ELTA3, persisted plugin/builder ID `2`.
- FTS3XX (Fennel FTS3XX protocol), persisted plugin ID `3`.

The public headers under `include/` contain no Zephyr types. ABI-visible
structures use fixed-width fields and explicit reserved space. The only
required service is `EF800_PORT_SERVICE_ID`; plugins never own a UART.
Standalone package and host-test builds use the minimal ABI shim under
`compat/`; consuming applications use PluginManager's authoritative header.

## Host Integration

Register one `struct pm_service` with UUID `EF800_PORT_SERVICE_ID`, version
`EF800_PORT_SERVICE_VERSION`, and a `struct ef800_port_api` vtable. Its
`write` callback returns the number of bytes written. Its `read` callback
returns `0` for timeout or the number of bytes read; these plugins request one
byte at a time with a 1000 ms timeout.

After PluginManager creates an instance, cast the opaque pointer to
`struct ef800_measurement_instance *`, validate `instance->api->abi_version`
and `struct_size`, then call:

- `get_serial_defaults(instance, defaults)` before configuring the claimed
  physical port.
- `measure(instance, result)` for a structured result in millimetres and gon
  multiplied by 10000.
- `render(instance, display)` for the bounded package-owned display model.
- `handle_key(instance, event, result)` for logical package input. Raw keyboard
  framing and LCD hardware remain host-owned.

## Measurement Submenus

MainApplication exposes external measurement packages through package-declared
submenus. Elta3 declares `Measure`, `Up`, `Down`, and `Enter`. FTS3XX is the
product name for the FTS3XX protocol adapter and declares `Measure` only. Its
protocol remains a binary `0x11` request followed by `CR LF` and a line-based
measurement response; directional input is intentionally unsupported.

The host keeps raw keyboard framing and UART ownership. While a plugin submenu
is active, host back/cancel and global keys have precedence. The host routes
the remaining logical action keys to the selected plugin binding and tears the
binding down when the submenu exits or the assignment becomes invalid.

The service table and Port vtable/context must remain valid until
`pm_manager_destroy_instance()` has completed.

## Build And Test

Host tests:

```sh
cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Cortex-M0+ packages, using either the application packaging script or an
optional PluginManager linker script:

```sh
cmake -S . -B build/arm \
  -DPM_ARM_TOOLCHAIN_PREFIX=/path/to/arm-zephyr-eabi \
  -DPLUGIN_MANAGER_DIR=/path/to/PluginManager
cmake --build build/arm
```

The output files are `elta3_plugin.pmp` and `fts3xx_plugin.pmp`. Every package
build invokes `tools/verify_package.py`, which verifies PMP metadata, CRC,
ARM ELF32 `ET_REL`, no undefined symbols, descriptor identity, and required
Port-service UUID agreement.

`tools/binary_to_c.py package.pmp output.c --symbol elta3_package` embeds a
package for static registration. `tools/instrument_simulator.py` requires
pyserial and simulates either instrument on a PC serial port.
