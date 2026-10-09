# native-sim-plugin-deployment Specification

## Purpose

Provide bounded native-sim plugin packaging and browser-mediated installation with explicit restart activation.

## Requirements

### Requirement: Build native-sim plugin packages
The project SHALL provide a native-sim plugin package profile that produces `.PMP` packages with the native_sim executable format and EF800 plugin contract, distinct from physical ARM-targeted packages.

#### Scenario: Build a native-sim package
- **WHEN** the native-sim plugin package build is invoked for a supported plugin
- **THEN** it produces a package with a native_sim-compatible ELF payload, valid PluginManager manifest, EF800 contract metadata, and target identification that the native simulator can validate

#### Scenario: Reject a physical-target package
- **WHEN** a physical ARM package is submitted to the native-sim package path
- **THEN** the package is rejected as target-incompatible before execution

### Requirement: Upload packages through the browser gateway
The web gateway SHALL accept bounded native-sim `.PMP` uploads, validate their name, size, target metadata, integrity, and package structure, and transfer accepted data to the connected simulator through a bounded control protocol.

#### Scenario: Upload a valid package
- **WHEN** a localhost browser uploads a valid native-sim package within the configured size limit
- **THEN** the gateway transfers it to the simulator and reports an installed package with restart-required state

#### Scenario: Upload an invalid package
- **WHEN** an upload has an invalid suffix, path, size, hash, manifest, ELF target, or plugin contract
- **THEN** the gateway rejects it without creating an executable package in simulator storage and reports a diagnostic reason

#### Scenario: Upload is interrupted
- **WHEN** a package upload disconnects or misses a chunk
- **THEN** the simulator retains no partially committed final package and the browser can retry with a new upload

### Requirement: Install packages atomically into virtual FAT storage
The simulator SHALL write accepted packages to a temporary file in the virtual FAT plugin directory, validate the complete package, and atomically rename it to its final `.PMP` name before startup discovery.

#### Scenario: Commit a package
- **WHEN** all upload chunks arrive and the package passes validation
- **THEN** the simulator atomically replaces or creates the final package file and marks the simulator restart-required

#### Scenario: Package commit fails
- **WHEN** virtual storage is unavailable, full, or unable to complete the rename
- **THEN** the package remains unavailable, the previous final package is preserved, and the browser receives an error

### Requirement: Activate packages only after explicit simulator restart
The browser SHALL expose a separate restart operation, and a newly installed package SHALL become discoverable only after the simulator restarts and MainApplication performs its normal startup scan.

#### Scenario: User restarts after upload
- **WHEN** the user presses Restart simulator after a successful package upload
- **THEN** the active simulator session shuts down through its supervised restart path, restarts, scans the plugin directory, and reports the package's registry state

#### Scenario: Package is installed but not restarted
- **WHEN** a package is installed and the user does not restart the simulator
- **THEN** the currently running plugin registry and active sessions remain unchanged while the browser shows restart-required state
