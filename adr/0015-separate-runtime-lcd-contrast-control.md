# Separate Runtime LCD Contrast Control from Display Content

- Status: accepted
- Date: 2026-10-29
- Supersedes: None

## Context

Modified EF800 hardware controls LCD contrast with a filtered PWM input, while unmodified units retain static contrast. MainApplication and BackupApplication share private Configuration storage but have distinct startup paths. The virtual LCD contract deliberately represents display content as a packed one-bit framebuffer, and the browser gateway owns presentation. The contrast feature must not activate PB8 on units configured for static contrast or turn analog contrast into simulated pixel content.

## Decision

Runtime LCD contrast enablement is a hardware capability state (`LCDCONTR0` or `LCDCONTR1`) separate from the target-composition profile. When enabled, both firmware applications use one shared contrast-control behavior and a persisted scalar in private Configuration storage; when disabled, firmware leaves PB8 uninitialized and retains the saved scalar for later re-enablement. Contrast output uses the selected hardware PWM path independently of T6963 display commands.

Native-sim preserves the T6963's logical one-bit framebuffer and communicates contrast state separately to the gateway. The browser applies contrast as a presentation effect, with the agreed 78% duty as its normal-contrast reference. Simulator contrast rendering remains behavioral evidence, not physical voltage or LCD qualification.

## Consequences

- MainApplication and BackupApplication must share load, default, apply, disable, and persistence semantics for contrast.
- LCDCONTR state can change live without changing the restart-activated target-composition profile.
- The PWM pin must be activated only when LCDCONTR1 is selected; static-contrast configurations must not initialize PB8.
- Native-sim and browser changes must preserve the stable one-bit framebuffer and LCD frame payload, carrying presentation state separately.
- The modified board's 40 kHz PWM and filtered voltage still require measurement on hardware; native-sim cannot qualify electrical behavior.
