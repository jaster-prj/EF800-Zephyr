# Select Battery SOC Source Through Composition

- Status: accepted
- Date: 2026-10-30
- Supersedes: None

## Context

EF800 targets can use different battery SOC sources: no SOC source, an ADC voltage estimate, or a MAX17048-compatible fuel gauge. MainApplication currently reads the gauge directly, while target composition is the established restart-scoped mechanism for selecting target hardware and behavior. The virtual target must exercise these same production source paths without introducing a second battery implementation.

The ADC estimate requires a chemistry-specific lookup table calibrated in millivolts at the ADC pin. Calibration is target data, not a hardware-source selection. A gauge may be absent because it is unpowered when the battery is disconnected, so runtime availability must not rewrite persistent COMPOSITION.

## Decision

Battery SOC acquisition SHALL be selected by one mutually exclusive `BATSOC` COMPOSITION group:

- `BATSOC0` selects no SOC source.
- `BATSOC1` selects the PC1 ADC source and a separately stored, validated calibration table.
- `BATSOC2` selects the MAX17048-compatible fuel gauge and is the default.

MainApplication SHALL use a common runtime battery snapshot regardless of source. Source availability and read failures are runtime state, not composition changes. If BATSOC2 cannot be probed at startup, the application reports SOC unavailable for that boot and probes again after restart. ADC calibration remains separate from COMPOSITION and is managed through a bounded shell interface.

Native-sim SHALL model the same source choices through Zephyr device APIs. Its browser SOC input SHALL update a shared virtual battery state that drives the selected MAX17048-compatible response or, for BATSOC1, an ADC-emulated PC1 voltage derived from the active lookup table.

## Consequences

- Target profiles can distinguish no SOC, ADC estimation, and gauge SOC while preserving the existing versioned COMPOSITION persistence and restart boundary.
- A missing gauge cannot block startup or silently change the saved profile; the LCD reports no SOC until the next successful sample or restart.
- ADC-based SOC is an estimate whose accuracy depends on a verified PC1 divider, ADC reference, and measured chemistry-specific calibration. Native-sim does not qualify those electrical properties.
- PC1 is available on the current BK1-only QSPI configuration. A future BK2 flash may conflict with PC1 and requires an explicit pin/resource review before activation.
- The native-sim UI and source models remain behavioral tools and do not claim to emulate the MAX17048's internal fuel-gauge algorithm.
