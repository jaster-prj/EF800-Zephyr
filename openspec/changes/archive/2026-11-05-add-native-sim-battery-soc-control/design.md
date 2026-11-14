## Context

EF800 Application currently initializes the MAX17048-compatible gauge at startup and samples its SOC/voltage through `shared/ef800_runtime`. The runtime snapshot and LCD icon already provide a source-neutral place to publish availability and SOC. The composition catalog currently has no BATSOC group.

The native-sim battery profile already carries SOC and voltage and can mark the emulated gauge unavailable, but the web page only configures serial instrument dummies. The native-sim overlay has no ADC device. Zephyr provides an ADC emulator that can be used to exercise the production ADC API rather than bypassing it.

The current physical board description routes QSPI through BK1 and does not assign PC1. The user reports that the second flash is not fitted; if it is later added on BK2, PC1 may become a conflicting resource. The battery divider values are 22 kOhm and 100 kOhm and the LiFePO4 pack maximum is 3.6 V, but the divider orientation and measured lookup points are not yet confirmed.

The design follows the accepted decisions to keep one production EF800 Application for physical and native-sim builds (ADR-0006), exercise production device APIs through native peripheral adapters (ADR-0005), use the local web gateway and its single bounded simulator connection (ADR-0004), and activate composition only on restart (ADR-0012, ADR-0016). Native-sim results remain behavioral evidence only (ADR-0003).

## Goals / Non-Goals

**Goals:**

- Select one battery SOC source through `BATSOC0`, `BATSOC1`, or `BATSOC2`, with `BATSOC2` as the default.
- Preserve the current no-SOC behavior when the selected gauge is unavailable, including the agreed startup and three-consecutive-read-failure policies.
- Read ADC pin voltage on PC1 and estimate SOC from a validated, shell-configurable table whose voltages are expressed in millivolts at that pin.
- Let the native-sim browser set SOC percent and exercise the selected production source path through a bounded simulator control message.
- Distinguish a valid SOC of 0% from no source or a failed source.

**Non-Goals:**

- Claiming that native-sim validates ADC electrical accuracy, divider behavior, battery chemistry, or physical PCB connectivity.
- Adding an automatic LiFePO4 table or production calibration points before the divider orientation and measured battery curve are verified.
- Automatically retrying a MAX17048 that was absent at startup, changing COMPOSITION at runtime, or making the SOC browser control persistent across simulator restarts.
- Emulating the MAX17048's internal electrochemical algorithm; native-sim will model its device response and the selected SOC input.

## Decisions

### Make BATSOC an explicit, restart-scoped source selection

Add mutually exclusive `BATSOC0` (no source), `BATSOC1` (PC1 ADC plus lookup table), and `BATSOC2` (MAX17048) entries to the existing COMPOSITION catalog. Register `BATSOC2` as the physical and native-sim default. The stored token records the chosen source; source availability is runtime state and must not rewrite the profile.

Provide a narrow physical shell selector for the BATSOC group through the existing COMPOSITION service. It stages and commits only the source choice, preserves unrelated profile tokens, and reports that restart is required; it does not change the default or alter source selection during the current boot.

This fits the existing composition contract and prevents incidental gauge detection from changing persistent configuration. A missing `BATSOC2` gauge degrades to unavailable SOC for that boot; a restart probes again. `BATSOC0` skips both acquisition paths. `BATSOC1` with no valid table or ADC reading also reports unavailable SOC rather than guessing.

### Normalize providers into the existing battery snapshot

Keep one runtime battery snapshot and LCD indicator. A source adapter returns a valid SOC/voltage sample or an explicit unavailable/error status. This avoids separate UI code for MAX17048 and ADC. An unavailable source is distinct from a valid zero-percent reading.

For `BATSOC2`, make one deferred probe from the runtime battery worker after application initialization; do not perform I2C gauge probing synchronously on the EF800 Application startup thread. If the probe fails, keep SOC unavailable without retrying until restart. After the first valid reading, keep the last good SOC through two consecutive read failures, clear availability on the third, and reset the count on success.

### Store ADC calibration separately from COMPOSITION

COMPOSITION selects the source; a separate private configuration record stores the calibration. The shell interface will expose a `battery soc-table` inspection/set operation. The proposed version-1 hex record is a contiguous encoding:

```text
version(1) | chemistry(1) | count(1) |
count * [PC1_mV_u16_be | SOC_u8] | CRC32_u32_be
```

Chemistry codes are `00` custom, `01` Li-ion, and `02` LiFePO4. The table is bounded to 2–16 points, with strictly increasing PC1 millivolts and SOC percentages from 0 to 100. CRC-32/ISO-HDLC covers all preceding bytes. The table record is verified and stored with recoverable slots; an invalid update leaves the last valid table intact. A valid update takes effect on the next ADC sample without changing COMPOSITION.

The forward conversion interpolates SOC from measured PC1 millivolts. The native-sim SOC slider uses inverse interpolation to derive PC1 millivolts from the same table, ensuring both directions exercise one calibration. No production table is supplied until the resistor orientation and calibration points are confirmed.

### Use the Zephyr ADC API and emulator in native-sim

Enable the physical ADC channel corresponding to PC1 (STM32L496 ADC input channel 2) for the `ef800` target. In native-sim, provide an ADC-emulated channel with the same logical channel contract. The ADC adapter converts the SOC control through the configured table into a millivolt input; the production runtime then reads that voltage via `adc_read` and performs the normal forward lookup.

The present BK1 QSPI configuration uses other pins, so PC1 is usable on the current board as reported. The design reserves a hardware review if BK2 is ever populated: PC1's alternate function can conflict with a second QSPI flash and both functions cannot be active on the same pad.

### Keep the browser input bounded and gateway-owned

Add a battery SOC control with integer range 0–100 to the native-sim front panel. Only the active controller may update it. The web gateway sends the value over the existing bounded simulator-control connection; the browser never connects directly to the simulator socket or writes the simulator filesystem.

The virtual battery profile must have an explicit `input_valid`/availability state so 0% is not confused with an unset override. Under `BATSOC2`, the virtual MAX17048-compatible response returns the requested SOC and valid version/VCELL register data. Under `BATSOC1`, the same SOC is converted into the emulated PC1 voltage through the table. The SOC input resets to the existing simulator default (80%) at process restart and is not written to target configuration.

## Risks / Trade-offs

- [The divider orientation is unknown, so a safe PC1 voltage cannot be derived from pack voltage yet] -> Keep table values in ADC-pin millivolts and do not provide calibrated production points until the 22 kOhm/100 kOhm placement, tolerances, VDDA margin, and measured curve are verified.
- [A future BK2 QSPI flash may use PC1] -> Treat BATSOC1/PC1 as the current board allocation only; require pin reassignment or an explicit resource decision before populating BK2.
- [LiFePO4 voltage is a weak SOC indicator over part of its discharge curve] -> Label ADC-derived SOC as an estimate and allow chemistry-specific measured tables; do not claim gauge-grade accuracy.
- [I2C failures may be transient] -> Keep the last valid SOC through two consecutive failures and clear it only on the third; expose the latest sample error for diagnostics.
- [A zero-percent virtual SOC could be mistaken for disabled input] -> Store availability separately from SOC value and test 0%, 1%, and 100% explicitly.
- [MAX17048 VCELL does not follow a chemistry model from an SOC-only slider] -> Keep VCELL as the existing independent virtual voltage value; do not claim the simulator models the gauge's internal SOC algorithm.

## Migration Plan

1. Add the BATSOC group with `BATSOC2` default. Existing profiles omit the group and therefore resolve to the default; older firmware preserves the new token as unknown.
2. Introduce the source-neutral runtime adapter and validate BATSOC2 missing-gauge and repeated-read-failure behavior before enabling the ADC path.
3. Add versioned calibration persistence and shell inspection/update. An absent/invalid table leaves BATSOC1 selected but SOC unavailable; installing a valid table enables the next sample.
4. Add physical PC1 ADC and native-sim ADC-emulator paths, then the native-sim SOC control and gateway route.
5. Rollback is firmware rollback: the existing composition service preserves unknown BATSOC tokens. Remove only calibration records through a deliberate maintenance operation if a later format migration requires it; do not reformat storage.

## Open Questions

- Confirm whether 22 kOhm is the battery-side or PC1-to-ground resistor before calculating pin voltage or publishing default calibration points.
- Measure representative LiFePO4 pack voltages at known SOC levels and confirm the ADC reference/VDDA and divider tolerances before a production lookup table is approved.
- The browser SOC input controls the MAX17048 SOC register while VCELL remains the existing independent simulator voltage. A coupled Li-ion SOC/voltage model can be added later if test scenarios require electrochemical consistency.
