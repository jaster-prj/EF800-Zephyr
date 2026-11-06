## ADDED Requirements

### Requirement: Select battery SOC acquisition through COMPOSITION
The MainApplication composition catalog SHALL define mutually exclusive battery SOC selections `BATSOC0`, `BATSOC1`, and `BATSOC2`. `BATSOC0` SHALL select no SOC source, `BATSOC1` SHALL select the ADC and calibration-table source, and `BATSOC2` SHALL select the MAX17048-compatible fuel-gauge source. `BATSOC2` SHALL be the registered default for physical EF800 and native-sim targets.

#### Scenario: No battery selection is stored
- **WHEN** a target profile has no recognized BATSOC token
- **THEN** the effective composition selects `BATSOC2`

#### Scenario: A BATSOC selection is stored
- **WHEN** a valid target profile contains exactly one supported BATSOC token
- **THEN** the composition resolver activates that source selection for the next application startup

#### Scenario: Multiple BATSOC selections are stored
- **WHEN** a profile attempts to select more than one token in the BATSOC group
- **THEN** composition validation rejects the conflicting selection rather than activating multiple SOC sources

#### Scenario: The selected MAX17048 is unavailable
- **WHEN** `BATSOC2` is selected but the gauge is unavailable at startup
- **THEN** runtime SOC behavior degrades to unavailable without rewriting the stored or effective BATSOC selection
