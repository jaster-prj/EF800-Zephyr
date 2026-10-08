# Use Native-Sim Package Control with an Explicit Restart Boundary

- Status: accepted
- Date: 2026-10-08
- Supersedes: None

## Context

The Virtual EF800 already runs production MainApplication in a dedicated native_sim container and exposes a localhost web gateway. Plugin discovery is intentionally startup-only because package files, plugin instances, UART ownership, and workspace resources must not be changed while active. Physical `.PMP` packages target ARM, while native_sim executes a host-native binary and needs a separate package target profile.

The browser must be able to install native-sim packages and configure virtual devices without switching USB storage modes or receiving Docker-socket access. A host bind mount is not the Zephyr virtual FAT filesystem used by production plugin discovery.

## Decision

Native-sim plugins will be built as a distinct native executable `.PMP` profile and will never be treated as physical ARM packages. Browser package management will use the private simulator control connection owned by the gateway. The simulator will validate uploads, write them atomically into its virtual FAT plugin directory, and report restart-required state.

Plugin activation will occur only after an explicit browser restart operation. A supervised simulator runtime will preserve the virtual flash volume while restarting the native_sim process, allowing normal MainApplication startup discovery to load the newly installed package.

Virtual device docking will use the same bounded simulator control boundary and existing host-owned Port service, rather than exposing filesystem, UART, or Docker internals to the browser or plugin.

## Consequences

- Native-sim plugin behavior can be demonstrated end-to-end without physical hardware or USB mode transitions.
- Native-sim PMP execution remains behavioral evidence and does not qualify ARM package loading, MPU isolation, or production package authenticity.
- The browser gateway needs bounded package-chunk, restart, and virtual-device control messages.
- Package installation must handle temporary files, hash validation, capacity limits, atomic rename, and interrupted transfers.
- A restart is visible and intentional; hot replacement is not supported by this decision.
