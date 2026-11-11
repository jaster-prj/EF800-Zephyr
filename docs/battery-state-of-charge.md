# Battery state of charge

MainApplication selects one SOC acquisition path through the restart-scoped
`BATSOC` COMPOSITION group:

| Token | Source | Behavior |
| --- | --- | --- |
| `BATSOC0` | None | Does not acquire battery SOC; the runtime snapshot reports it unavailable. |
| `BATSOC1` | ADC on PC1 | Converts the PC1 voltage in millivolts through the installed calibration table. Without a usable ADC or table, SOC remains unavailable. |
| `BATSOC2` | MAX17048-compatible gauge | Default for physical EF800 and native-sim. The gauge is probed once from runtime work after application initialization. |

An absent gauge does not change the stored composition and is not retried during
that boot. Restarting the application allows one new probe. After the first
valid gauge sample, the runtime keeps the last SOC through two consecutive read
failures and clears SOC availability on the third. A valid 0% reading remains
distinct from an unavailable source.

## PC1 calibration table

Each successful `BATSOC1` acquisition contributes one PC1 reading to an
8-sample moving-average ring buffer. The SOC lookup and reported voltage use
the mean of the readings currently in the buffer; startup uses the samples
available so far rather than waiting for a full window. `BATSOC2` is unaffected.

No factory-measured LiFePO4 calibration is available. The EF800 target's
22 kOhm / 100 kOhm divider is wired with 22 kOhm on the battery side and
100 kOhm from PC1 to ground. The table below is a generic, approximate 1S
LiFePO4 resting-voltage curve converted for that divider; it is not a measured
target calibration. ADC-derived SOC must be treated as an estimate, not as
gauge-grade accuracy. The PC1 allocation also needs review before fitting the
currently unused second QSPI flash on BK2.

### Generic approximate 1S LiFePO4 reference

These are illustrative open-circuit cell voltages near 25 C, after at least
30 minutes without charge or load. Actual SOC depends on the cell, temperature,
history, and load; the flat middle of the LiFePO4 curve makes voltage-only SOC
particularly imprecise. Use the cell manufacturer's curve when available.

| SOC | Cell OCV | PC1 voltage for 22 kOhm battery-side / 100 kOhm to GND |
| ---: | ---: | ---: |
| 0% | 2.50 V | 2049 mV |
| 10% | 3.00 V | 2459 mV |
| 20% | 3.20 V | 2623 mV |
| 30% | 3.22 V | 2639 mV |
| 40% | 3.25 V | 2664 mV |
| 50% | 3.26 V | 2672 mV |
| 60% | 3.27 V | 2680 mV |
| 70% | 3.30 V | 2705 mV |
| 80% | 3.32 V | 2721 mV |
| 90% | 3.35 V | 2746 mV |
| 100% | 3.40 V | 2787 mV |

The PC1 values use `V_PC1 = V_cell * 100 / (22 + 100)` and are rounded to the
nearest millivolt. They are within the configured 0-3300 mV ADC-table range.
This reference curve is based on a representative published LiFePO4 resting
voltage chart, not on EF800 hardware measurements:
[EVE LiFePO4 SOC chart](https://www.evemall.eu/selection-guide/eve-lifepo4-state-charge-chart-discharge-curve-diagrams).

The exact version-1 table string for the points above (chemistry `02` =
LiFePO4, CRC-32/ISO-HDLC included) is:

```text
01020b080100099b0a0a3f140a4f1e0a68280a70320a783c0a91460aa1500aba5a0ae36473f7f18d
```

Install it with `battery table set <string>` only when this approximate
voltage-based estimate is acceptable. This is not a battery protection limit;
do not use it as a substitute for a BMS or a calibrated fuel gauge.

Calibration is installed and inspected with the MainApplication shell:

```text
battery table show
battery table set <contiguous-hex-string>
```

The serial shell is available by default on the DebugConsole. To transfer files
with the raw serial MCUmgr filesystem protocol instead, enter `mcumgr start`;
that mode pauses shell input until MainApplication is reset.

Select the source through the physical target's COMPOSITION-backed shell
command:

```text
battery source get
battery source set BATSOC1
```

The selection is committed for the next restart; it does not switch acquisition
sources in the current boot. LCD contrast composition is selected separately
with `lcdcontrast mode LCDCONTR1`. COMPOSITION permits one committed change per
boot, so apply and restart between these two commands when both groups need to
change.

The version-1 binary record, represented as one contiguous hexadecimal string,
is:

```text
version:u8 | chemistry:u8 | count:u8 |
count * [PC1_mV:u16_be | SOC_percent:u8] | CRC32:u32_be
```

Chemistry codes are `00` custom, `01` Li-ion, and `02` LiFePO4. The table must
contain 2–16 points; PC1 millivolts must be strictly increasing and in the
0–3300 mV range; SOC must be strictly increasing from a first point of 0% to a
last point of 100%. The CRC is CRC-32/ISO-HDLC over all preceding bytes. Forward
and inverse conversion use linear interpolation and clamp values beyond the
table endpoints. Malformed or unverified updates do not replace the previously
valid table.

The table is stored separately from COMPOSITION in alternating, CRC-verified
configuration slots. Installing a table does not change source selection;
`BATSOC1` becomes usable on its next sample once both the ADC and a valid table
are available.

## Native-sim behavior

The Virtual EF800 front panel accepts integer SOC values from 0 through 100
through the gateway. Only the active controller can submit updates; the browser
does not connect directly to the simulator socket. The control is disabled for
viewers, while disconnected, and when `BATSOC0` is active.

With `BATSOC2`, the virtual MAX17048-compatible device reports the configured
SOC and its valid VCELL value. With `BATSOC1`, the configured SOC is inverse-
interpolated through the active table into the Zephyr ADC emulator, and the
normal MainApplication ADC read and forward lookup are used. With `BATSOC0`, no
SOC is published. The simulator input is transient and resets to 80% when the
simulator process starts; it is not saved in target configuration.

Native-sim verifies software behavior only. It does not qualify the physical
divider, ADC electrical accuracy, battery chemistry curve, or board wiring.
