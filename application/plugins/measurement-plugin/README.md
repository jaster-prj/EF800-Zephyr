# EF800 Measurement Plugin SDK

This application-owned SDK builds and tests two standalone PluginManager
measurement packages. Each package owns its measurement submenu declaration,
logical keyboard handling, measurement state, and bounded display model:

- ELTA3, persisted plugin/builder ID `2`.
- FTS3XX (Fennel FTS3XX protocol), persisted plugin ID `3`.

The public headers under `include/` contain no Zephyr types. ABI-visible
structures use fixed-width fields and explicit reserved space. Metadata v3
declares the execution model, isolation ABI, target, required worker services,
mailbox size, minimum plugin RAM/worker stack, and MPU alignment. ELTA3 and
FTS3XX now build as `USER_WORKER` packages and use the bounded worker SDK; they
do not receive a host callback or context pointer. Their build/package checks
do not qualify L496 MPU, fault-recovery, or normal-protocol behavior on target.
Standalone package and host-test builds use the minimal ABI shim under
`compat/`; consuming applications use PluginManager's authoritative header.

## Worker Integration

The package receives the pointer-free `EF800_PLUGIN_WORKER_SERVICE` binding
only during worker create. It copies the mailbox address, generation, and opaque
capability into its private instance and performs serial I/O through
`ef800_plugin_worker_port_write()` and `ef800_plugin_worker_port_read()`. The
host verifier checks the request and dispatches it through the active
host-owned assignment and UART transport. The SDK read helper requests one byte
at a time with a bounded 1000 ms timeout for the existing instrument protocols.

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

The binding is a worker-side SDK object, not a privileged service vtable. The
shared mailbox ABI and host-validated capabilities are defined in
`shared/ef800_plugin/ef800_plugin_worker_abi.h`.

The isolated SDK surface is `include/ef800_port_service.h` and
`shared/ef800_plugin/ef800_plugin_worker_sdk.h`. It issues bounded mailbox
requests through the single imported `ef800_plugin_worker_syscall` symbol;
MainApplication resolves that import to a user-callable SVC-only bridge. The
privileged syscall verifier checks the active worker/session and copies data
through the mailbox before accessing host-owned transport or workspace state.
It does not place a host callback or context pointer in the plugin service
table. `ef800_legacy_port_service.h` remains only for trusted development
compatibility code and is not included by these packages.

## Build And Test

Host tests:

```sh
cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Build the independently packaged native_sim ELTA3 and FTS3XX images and run
their protocol plus PMP identity/architecture checks with:

```sh
cmake -S . -B build/native-sim \
  -DPM_BUILD_NATIVE_SIM=ON \
  -DPLUGIN_MANAGER_DIR=/path/to/PluginManager
cmake --build build/native-sim
ctest --test-dir build/native-sim --output-on-failure
```

The native_sim artifacts use the constrained i386 relocatable profile. The
package runtime test loads each `.pmp` through PluginManager, creates and
destroys its instance through the executor API, then validates serial defaults
and a measurement response. These tests exercise package parsing, relocation,
descriptor binding, and lifecycle behavior, but are not evidence of ARM MPU
isolation.

Cortex-M4 packages, using either the application packaging script or an
optional PluginManager linker script:

```sh
cmake -S . -B build/arm \
  -DPM_ARM_TOOLCHAIN_PREFIX=/path/to/arm-zephyr-eabi \
  -DPLUGIN_MANAGER_DIR=/path/to/PluginManager
cmake --build build/arm
```

The output files are `elta3_plugin.pmp` and `fts3xx_plugin.pmp`. Every package
build invokes `tools/verify_package.py`, which verifies PMP metadata, CRC,
ARM ELF32 `ET_REL`, no undefined symbols, descriptor identity, target matching,
and the declared execution model and memory/service requirements. A successful
legacy-package verification does not qualify the package for isolated
execution.

`tools/binary_to_c.py package.pmp output.c --symbol elta3_package` embeds a
package for static registration. `tools/instrument_simulator.py` requires
pyserial and simulates either instrument on a PC serial port.
