# battery-state-of-charge Specification

## Purpose

TBD - Define battery state-of-charge acquisition, fallback, calibration, and reporting behavior.

## Requirements

### Requirement: Provide battery SOC from the selected BATSOC source
EF800Application SHALL use the active COMPOSITION selection to choose exactly one battery SOC source: `BATSOC0` means no SOC source, `BATSOC1` means the PC1 ADC input converted through the active lookup table, and `BATSOC2` means the MAX17048-compatible fuel gauge. A missing or failed source SHALL be represented as unavailable, not as a valid zero-percent reading.

#### Scenario: SOC is read from the MAX17048 source
- **WHEN** `BATSOC2` is active and the gauge returns a valid sample
- **THEN** EF800Application publishes the gauge SOC and voltage through the common runtime battery snapshot

#### Scenario: SOC is estimated from the ADC source
- **WHEN** `BATSOC1` is active, the ADC and lookup table are valid, and an ADC sample is available
- **THEN** EF800Application converts the PC1 pin voltage in millivolts to SOC using interpolation in the active table

#### Scenario: No SOC source is selected
- **WHEN** `BATSOC0` is active
- **THEN** EF800Application reports battery SOC as unavailable and does not display a valid zero-percent SOC

#### Scenario: Zero percent is a valid battery reading
- **WHEN** an available SOC source reports exactly 0 percent
- **THEN** EF800Application marks the sample valid and distinguishes it from `BATSOC0` or a failed source

### Requirement: Fall back safely when the selected SOC source is unavailable
When `BATSOC2` is active, EF800Application SHALL make one deferred MAX17048 probe from runtime work after application initialization, without blocking startup. If the gauge does not respond, EF800Application SHALL behave as no-SOC for that boot, SHALL preserve the selected `BATSOC2` composition, and SHALL NOT retry until the next restart. When `BATSOC1` is active but its ADC or calibration table is unavailable or invalid, EF800Application SHALL report SOC as unavailable and SHALL preserve the selected composition.

#### Scenario: MAX17048 is absent at startup
- **WHEN** `BATSOC2` is active and its single deferred runtime probe fails
- **THEN** EF800Application continues startup without SOC, preserves `BATSOC2`, and does not retry the gauge during that boot

#### Scenario: ADC calibration is not available
- **WHEN** `BATSOC1` is active but there is no valid calibration table or readable ADC input
- **THEN** EF800Application continues without SOC, reports the reason, and does not invent a default percentage

### Requirement: Retain the last valid SOC through transient gauge failures
After at least one valid `BATSOC2` sample, EF800Application SHALL retain the last valid SOC during the first two consecutive read failures, SHALL clear SOC availability on the third consecutive failure, and SHALL reset the failure count after any successful sample. Failures before the first valid reading SHALL leave SOC unavailable immediately.

#### Scenario: One or two gauge reads fail
- **WHEN** one or two consecutive gauge reads fail after a valid reading
- **THEN** EF800Application retains the last valid SOC while recording that the latest sample failed

#### Scenario: Third consecutive gauge read fails
- **WHEN** the third consecutive gauge read fails after a valid reading
- **THEN** EF800Application marks SOC unavailable so the display clears the stale battery indication

#### Scenario: A valid read follows a failure
- **WHEN** a gauge read succeeds before the third consecutive failure
- **THEN** EF800Application updates SOC and resets the consecutive-failure count

### Requirement: Configure ADC SOC calibration through a bounded shell hex string
EF800Application SHALL provide shell operations to inspect and replace the active ADC SOC calibration using one contiguous hexadecimal string. The decoded record SHALL contain a format version, chemistry identifier, 2–16 lookup points, and a CRC-32. Each point SHALL contain unsigned 16-bit big-endian millivolts measured at the ADC pin and an unsigned SOC percentage. The record SHALL be rejected unless its length, version, chemistry, CRC, voltage range, strictly increasing pin-millivolt points, and strictly increasing SOC points from 0 through 100 percent are valid.

#### Scenario: A valid calibration is installed
- **WHEN** the shell receives a complete hex string with a supported version, valid CRC, and valid ordered points
- **THEN** it verifies and stores the complete table separately from COMPOSITION and makes it available to `BATSOC1`

#### Scenario: A malformed calibration is rejected
- **WHEN** the shell receives invalid hex, a bad length or CRC, an unsupported chemistry, out-of-range values, or unordered points
- **THEN** it rejects the update and retains the previously valid calibration

#### Scenario: Calibration is inspected
- **WHEN** the shell requests the active ADC SOC table
- **THEN** it reports the stored chemistry and point values in a form that can be used to verify or reproduce the hex record

#### Scenario: Calibration is changed while ADC SOC is active
- **WHEN** a valid table update is committed while `BATSOC1` is active
- **THEN** the next SOC sample uses the new verified table without changing the active COMPOSITION selection
