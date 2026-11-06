## 1. Composition and runtime source selection

- [x] 1.1 Add the mutually exclusive BATSOC0/BATSOC1/BATSOC2 catalog group with BATSOC2 default for physical EF800 and native-sim; verify profile resolution, persistence, browser composition presentation, and restart activation.
- [x] 1.2 Refactor MainApplication battery acquisition behind a source-neutral provider selected from active COMPOSITION; BATSOC0 skips acquisition, BATSOC1 uses ADC, and BATSOC2 probes the MAX17048-compatible gauge.
- [x] 1.3 Implement one deferred BATSOC2 probe after application initialization, with no profile mutation or same-boot retries, and add tests for absent and responding gauges.
- [x] 1.4 Track consecutive MAX17048 read failures: preserve the last valid SOC for two failures, clear availability on the third, reset on success, and keep unavailable distinct from valid 0%.
- [x] 1.5 Add a physical shell command to inspect and select BATSOC0/BATSOC1/BATSOC2 through COMPOSITION, preserving other tokens and requiring restart for activation.

## 2. ADC SOC calibration and physical input

- [x] 2.1 Implement version-1 hex table encode/decode and validation for chemistry, point count, PC1 millivolt/SOC ordering, bounds, and CRC-32; add malformed, boundary, and round-trip tests.
- [x] 2.2 Persist calibration separately from COMPOSITION with recoverable verified slots and add shell commands to set and inspect the contiguous hex table; verify invalid updates retain the prior valid table.
- [ ] 2.3 Verify the 22 kOhm/100 kOhm divider orientation, resistor tolerances, PC1 maximum voltage versus VDDA, and measured LiFePO4 voltage/SOC points before publishing any default calibration table.
- [x] 2.4 Configure the physical PC1 ADC channel and implement BATSOC1 sampling and forward lookup interpolation; ensure missing ADC or calibration reports SOC unavailable without changing COMPOSITION.

## 3. Native-sim battery models

- [x] 3.1 Add a native-sim ADC-emulated channel matching PC1's ADC contract and expose bounded millivolt input updates through the virtual battery model.
- [x] 3.2 Separate virtual battery availability from its SOC value so 0% is a valid sample, and update the MAX17048-compatible response from the browser-controlled SOC while retaining a valid VCELL value.
- [x] 3.3 Route BATSOC1's virtual SOC input through inverse lookup interpolation to the emulated PC1 voltage; verify the production ADC and lookup path returns the requested SOC within defined rounding tolerance.
- [x] 3.4 Add native tests for BATSOC0/1/2, MAX17048 register behavior, PC1 ADC conversion, SOC 0/100 boundaries, unavailable sources, and the three-failure stale-value policy.

## 4. Native-sim browser battery control

- [x] 4.1 Add a bounded gateway battery-control operation that accepts integer SOC 0–100 from the active controller and routes it over the existing simulator control connection.
- [x] 4.2 Add an accessible battery SOC control to the web front panel, show acceptance/unavailable errors, and disable updates for viewers or when BATSOC0 is active.
- [x] 4.3 Add gateway and web asset tests for valid updates, 0% as a valid value, out-of-range rejection, viewer rejection, disconnect behavior, and simulator restart resetting the transient input.
- [ ] 4.4 Run an interactive native-sim acceptance test for BATSOC2 register-backed SOC and BATSOC1 ADC-emulated SOC through the displayed MainApplication battery indicator.

## 5. Documentation and verification

- [x] 5.1 Document BATSOC selections, startup fallback, three-read failure policy, the PC1 calibration hex format, and the unqualified physical divider/table assumptions.
- [ ] 5.2 Build the native-sim and physical MainApplication targets, run the prescribed firmware builds and native Twister suite, and verify the virtual EF800 web deployment with both supported SOC source paths.
