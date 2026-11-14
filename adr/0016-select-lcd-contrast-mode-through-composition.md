# Select LCD Contrast Mode Through Composition

- Status: accepted, supersedes ADR-0015
- Date: 2026-10-29
- Supersedes: ADR-0015

## Context

ADR-0015 modeled `LCDCONTR0`/`LCDCONTR1` as a separate live hardware state. The repository's established COMPOSITION mechanism is now the required source for hardware configuration. COMPOSITION records are staged and verified during a boot, then become active only when the service initializes after restart. LCD contrast duty remains a separately adjustable runtime value, and the one-bit LCD framebuffer remains a presentation-independent content contract.

## Decision

LCD contrast mode SHALL be represented by mutually exclusive `LCDCONTR0` and `LCDCONTR1` selections in the `LCDCONTR` COMPOSITION group, with `LCDCONTR0` as the supported default. EF800 Application and EF800 Backup SHALL resolve the active composition at startup and initialize PB8 PWM only when `LCDCONTR1` is active. Console changes stage and commit the composition token and report restart-required; they do not reconfigure the running image. The duty percentage remains in its own private Configuration file and may be changed at runtime while LCDCONTR1 is active.

Native-sim preserves the T6963's logical one-bit framebuffer and communicates active composition mode and duty separately to the gateway. The browser applies contrast as a presentation effect, with 78% duty as its normal-contrast reference. Simulator rendering is behavioral evidence, not physical voltage or LCD qualification.

## Consequences

- LCDCONTR state and COMPOSITION profile processing use the same staged, verified persistence and restart-activation boundary.
- EF800 Application Settings visibility and PB8 setup are determined by the active composition at boot; a committed change requires restart before either changes.
- EF800 Application and EF800 Backup must resolve the same active profile, including the LCDCONTR0 default.
- The PWM duty file remains independent, is retained while LCDCONTR0 is active, and does not change the composition generation.
- The modified board's 40 kHz PWM and filtered voltage still require hardware measurement; native-sim cannot qualify electrical behavior.
