# Spec Delta

## ADDED Requirements

### Requirement: Declare and validate plugin-owned memory needs
Each external plugin package SHALL declare bounded requirements for its plugin-owned executable, read-only, writable data/BSS, stack, and plugin-owned shared buffers. EF800 Application SHALL validate the declaration against package contents, available memory, alignment, and MPU constraints before admitting the plugin.

#### Scenario: Plugin declaration fits the active memory profile
- **WHEN** a package has a valid memory declaration and all required regions fit the validated runtime memory profile and MPU layout
- **THEN** EF800 Application may load the package into regions that preserve the required access permissions

#### Scenario: Plugin declaration is missing or malformed
- **WHEN** a package omits a required memory bound or declares invalid, inconsistent, or out-of-policy sizes
- **THEN** EF800 Application rejects the package before executing plugin code and reports a diagnosable incompatibility

#### Scenario: Plugin needs exceed available memory
- **WHEN** a package's declared needs exceed available memory or cannot be represented by the MPU layout
- **THEN** EF800 Application reports the plugin unavailable without weakening isolation or preventing core application startup

#### Scenario: Plugin uses host-managed hardware services
- **WHEN** a plugin requests a host-mediated service such as Bluetooth
- **THEN** the plugin memory declaration covers only plugin-owned runtime memory and EF800 Application separately budgets and owns all memory used by the host service and hardware stack
