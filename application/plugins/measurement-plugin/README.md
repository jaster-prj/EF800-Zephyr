# EF800 Measurement Plugin SDK

This application-owned SDK builds and tests two standalone PluginManager
measurement packages:

- ELTA3, persisted plugin/builder ID `2`.
- FTS3XX, persisted plugin/builder ID `3`.

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
- `handle_input(instance, input)` for ELTA3 up/down/enter. FTS3XX returns
  `PM_ENOTSUP`.

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
