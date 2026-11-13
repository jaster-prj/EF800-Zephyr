## ADDED Requirements

### Requirement: Model the selected battery SOC source in native-sim
Native-sim SHALL provide a source-independent battery SOC input from 0 through 100 percent and SHALL route it through the active BATSOC source model. The simulator SHALL represent an unavailable source separately from a valid 0-percent SOC value.

#### Scenario: Native-sim uses BATSOC2
- **WHEN** `BATSOC2` is active and the virtual battery is available
- **THEN** the MAX17048-compatible device path returns valid version, SOC, and VCELL readings, with its SOC response matching the configured virtual SOC input

#### Scenario: Native-sim uses BATSOC1
- **WHEN** `BATSOC1` is active and a valid calibration table is loaded
- **THEN** the Zephyr ADC emulator supplies a PC1-equivalent input voltage derived by inverse interpolation from the configured SOC percentage and the active table

#### Scenario: Native-sim uses BATSOC0
- **WHEN** `BATSOC0` is active
- **THEN** neither the MAX17048-compatible gauge nor the ADC model supplies a valid SOC reading to EF800Application

#### Scenario: Virtual SOC input is zero
- **WHEN** the browser sets the virtual SOC input to 0 percent while BATSOC1 or BATSOC2 is active
- **THEN** the selected emulated source reports a valid zero-percent reading instead of interpreting zero as an unset override

#### Scenario: Virtual battery state is reset
- **WHEN** the native-sim process restarts
- **THEN** transient browser battery input returns to its documented simulator default and does not alter physical target configuration
