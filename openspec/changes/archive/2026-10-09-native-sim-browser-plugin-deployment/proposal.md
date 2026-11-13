## Why

The native-sim EF800 can already execute the production EF800Application and expose a browser front panel, but external plugin packages cannot yet be built for native_sim, uploaded through the browser, or activated against simulated instruments. This prevents an end-to-end demonstration of plugin discovery, port services, bounded UI, and virtual-device behavior without switching USB storage modes or using physical hardware.

## What Changes

- Add a native-sim package profile that produces executable `.PMP` plugins compatible with the native simulator rather than the physical ARM target.
- Add browser upload of native-sim packages through the web gateway and bounded simulator control protocol.
- Install packages atomically into the simulator's virtual FAT plugin directory and validate them before activation.
- Add a separate explicit **Restart simulator** operation; uploaded packages become active only after startup discovery on restart.
- Add browser-visible plugin inventory, validation state, restart-required state, and activation diagnostics.
- Add browser-configurable virtual instrument docking and bounded serial response profiles for plugin port testing.
- Preserve host-owned plugin UI, keyboard, port, workspace, and lifecycle boundaries.
- Keep the feature explicitly behavioral and native-sim-only; it does not qualify physical ARM plugin loading or production package security.

## Capabilities

### New Capabilities

- `native-sim-plugin-deployment`: Native-sim PMP build, browser upload, virtual-FAT installation, explicit restart activation, and plugin status.
- `virtual-device-docking`: Browser-controlled bounded virtual instrument profiles assigned to native-sim plugin ports.

### Modified Capabilities

- `virtual-ef800-deployment`: Extend the localhost web deployment with native-sim plugin management and restart control.
- `virtual-ef800-platform`: Extend the behavioral simulator interface with plugin lifecycle and virtual-device verification flows.

## Impact

- Plugin SDK/CMake packaging and PluginManager host adaptation for the native_sim target.
- Web gateway HTTP/WebSocket APIs and browser UI.
- Simulator control protocol, virtual FAT storage, restart supervision, and package discovery.
- Native-sim serial-peer/profile emulators and integration tests.
- Docker Compose runtime and deployment documentation.
- No physical EF800 image layout, USB ownership, or hardware qualification behavior changes.
