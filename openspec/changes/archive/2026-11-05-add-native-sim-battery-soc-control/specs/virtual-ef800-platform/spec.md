## ADDED Requirements

### Requirement: Set simulated battery SOC from the browser
The native-sim web front panel SHALL provide an accessible battery SOC control accepting integer percentages from 0 through 100. A controlling browser SHALL be able to submit the value through the gateway's bounded control interface; the browser SHALL NOT connect directly to the simulator socket.

#### Scenario: User changes simulated SOC with BATSOC2 active
- **WHEN** the controller submits a SOC percentage and `BATSOC2` is active
- **THEN** the gateway updates native-sim battery state so the MAX17048-compatible SOC response matches that percentage

#### Scenario: User changes simulated SOC with BATSOC1 active
- **WHEN** the controller submits a SOC percentage and `BATSOC1` is active with a valid calibration table
- **THEN** the gateway updates native-sim battery state so the ADC emulator supplies the corresponding calibrated PC1 millivolt value

#### Scenario: User changes simulated SOC with BATSOC0 active
- **WHEN** the controller submits a SOC percentage and `BATSOC0` is active
- **THEN** the front panel reports that no SOC source is active and EF800 Application continues to report SOC as unavailable

#### Scenario: A viewer or invalid request attempts an update
- **WHEN** a non-controller submits a battery update or the request contains a value outside 0–100
- **THEN** the gateway rejects the update and leaves simulator battery state unchanged

#### Scenario: Battery input is updated
- **WHEN** a valid controller update is accepted
- **THEN** the browser reports acceptance and EF800 Application reflects the selected source's resulting SOC within its normal battery-sampling interval
