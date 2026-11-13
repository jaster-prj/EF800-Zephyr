## Why

EF800Application currently reads SOC only through the MAX17048-compatible gauge, while another EF800 battery configuration has no SOC source and may later estimate SOC from an ADC input. Native-sim already has simulated battery values, but the web front panel cannot set them or exercise either source path, making battery-source behavior difficult to verify interactively.

## What Changes

- Add the mutually exclusive `BATSOC0` (no SOC), `BATSOC1` (ADC plus calibrated lookup table), and `BATSOC2` (MAX17048) composition selections, with `BATSOC2` as the default.
- Let EF800Application degrade to no-SOC behavior when the selected MAX17048 is absent at startup, without changing the saved composition or retrying until restart. After successful reads, retain the last SOC for two consecutive failures and clear it after the third.
- Add a shell interface for installing and inspecting a bounded, validated SOC lookup table encoded as a hex string. Table voltages are millivolts measured at the ADC pin; PC1 is the proposed EF800 ADC input.
- Add a native-sim web control for battery SOC percent. The selected virtual source will return a MAX17048-compatible response or derive the ADC-pin voltage by inverse interpolation through the configured table.

## Capabilities

### New Capabilities
- `battery-state-of-charge`: Select and sample the SOC source, manage ADC calibration, and degrade safely when a source is unavailable.

### Modified Capabilities
- `target-composition-profile`: Register the BATSOC source-selection group and its default.
- `native-sim-ef800-peripherals`: Model the selected MAX17048 or ADC battery input from the virtual battery state.
- `virtual-ef800-platform`: Expose native-sim battery SOC control in the browser and route it through the simulator control protocol.

## Impact

Affected areas include EF800Application composition and runtime sampling, the ADC and MAX17048 adapters, native-sim Devicetree/peripheral emulation, shell calibration commands, the virtual simulator control protocol, the web gateway/front panel, tests, and battery/composition documentation. PC1 is currently not used by the modeled BK1 QSPI path; the currently unfitted BK2 flash remains a future pin-ownership conflict. The actual divider orientation and calibrated LiFePO4 table points must be verified before publishing production calibration defaults.
