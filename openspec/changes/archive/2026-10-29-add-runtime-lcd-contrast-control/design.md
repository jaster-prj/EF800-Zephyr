## Context

The physical display path currently controls the T6963C over GPIO and produces no contrast-control signal. The EF800 board is clocked at 80 MHz; PB8 supports TIM4_CH3, and the agreed carrier is 40 kHz. On modified boards a flying wire connects the MCU-side JP15 pad to the contrast filter while JP15 remains open. LCDCONTR1 selects controllable hardware; LCDCONTR0 requires the existing static contrast and must leave PB8 uninitialized.

EF800 Application and EF800 Backup both mount the same private Configuration FAT at different times. EF800 Application already initializes the shared COMPOSITION service from the private Configuration FAT; EF800 Backup mounts `/EF800:` for recovery. Store LCDCONTR0/1 only as a token in the existing target-composition profile, and keep the contrast duty separately in `/EF800:/.configuration/contrast`. The COMPOSITION catalog default is LCDCONTR0, protecting units without an explicit opt-in.

The T6963 emulator intentionally publishes a packed one-bit framebuffer. The virtual simulator sends LCD frames to the host gateway; the browser currently paints those bits with fixed near-black and white colors. The gateway owns browser presentation, while the simulator binary LCD frame and logical framebuffer are architectural contracts.

The checked-in EF800 Application menu uses fixed item arrays. Its shell currently registers RTC and keyboard commands; no LCD contrast code or LCDCONTR symbols are present. The existing COMPOSITION service stages and commits file-backed profiles, and its accepted contract activates changes only at restart. LCDCONTR mode selection uses that service and preserves its restart boundary.

The repository's in-force ADRs include the composition profile's restart-only activation contract; ADR-0016 supersedes ADR-0015 to align LCDCONTR mode with it. Relevant constraints are: ADR-0004 assigns browser presentation to the gateway; ADR-0005/0006 require production EF800 Application execution and shared source composition in native_sim; ADR-0007 preserves the one-bit framebuffer and stable LCD frame contract; ADR-0011/0013 keep Configuration filesystem-mediated and separate from shared-data MTP; ADR-0012 defines staged, restart-activated composition profiles.

## Goals / Non-Goals

**Goals:**
- Generate and adjust 40 kHz PWM on PB8/TIM4_CH3 only while LCDCONTR1 is active, in both EF800 Application and EF800 Backup.
- Initialize the persisted duty to 78% on first enabled use, use subsequent saved values, and retain the saved file while disabled.
- Expose contrast adjustment in EF800 Application Settings only when the active composition selects LCDCONTR1 and through the console; console mode changes use COMPOSITION and require restart.
- Reflect simulator contrast in browser shades while preserving logical one-bit LCD pixels and the existing T6963 emulator behavior.
- Keep unmodified boards' static contrast behavior when LCDCONTR0 is selected.

**Non-Goals:**
- Changing the T6963C command model, packed framebuffer, or physical LCD data path.
- Adding a custom Configuration menu or a separate LCDCONTR mode file.
- Exposing private `.configuration` data through the native_sim browser MTP file view.
- Treating native_sim rendering or PWM calculations as electrical qualification of the modified PCB/filter.

## Decisions

1. **Represent LCDCONTR mode as a COMPOSITION group with a static default.** Add mutually exclusive `LCDCONTR0` and `LCDCONTR1` catalog entries in the `LCDCONTR` group, with LCDCONTR0 as default. EF800 Application stages/commits mode selections through the composition service; the active running mode and Settings menu do not change until the next restart, matching ADR-0012. EF800 Backup resolves the same active profile at startup.
   - Alternative considered: a separate `lcdcontr` mode file with immediate runtime switching. Rejected because the repository's COMPOSITION mechanism is the established hardware-configuration source and expressly requires restart activation.

2. **Use a shared contrast service for active mode, duty persistence, and output.** EF800 Application and EF800 Backup use the active composition result to decide whether to start PWM. Store only duty as a bounded integer percentage from 0 to 100 in `/EF800:/.configuration/contrast`. When LCDCONTR1 is active and no duty file exists, create it at 78%; an existing valid value takes precedence. Invalid duty values must never be passed to the PWM API; report the invalid record and use the 78% fallback. LCDCONTR0 does not delete or rewrite the duty file. The physical and Backup builds remain mutually exclusive owners of the Configuration FAT.
   - Alternative considered: duplicate parsing and PWM setup in each application. Rejected to avoid startup and persistence behavior drifting between the two firmware images.
   - Alternative considered: store the duty in the COMPOSITION profile. Rejected because duty is independently adjustable at runtime, while COMPOSITION changes intentionally require restart.

3. **Use STM32 timer PWM and defer PB8 alternate-function setup until enabled.** Configure TIM4 channel 3 for 40 kHz; the 80 MHz timer clock yields 2,000 timer counts per period, so 78% maps exactly to 1,560 counts and integer-percent control has fine resolution. Pinctrl must not mux PB8 to TIM4_CH3 during unconditional Zephyr device initialization: apply the PWM alternate function only when the active LCDCONTR1 composition is loaded and return PB8 to a non-driving state when LCDCONTR0 is active. Verify the selected Zephyr PWM/pinctrl arrangement against the actual STM32 driver, which applies its configured pinctrl state during device initialization; if that prevents deferred muxing, use a small EF800 timer/pin wrapper while retaining TIM4 hardware PWM.
   - Alternative considered: software PWM on GPIO. Rejected because PB8 has a suitable timer output and the requested stable 40 kHz carrier is a hardware-timer function.
   - Alternative considered: unconditional static PWM pinctrl. Rejected because it would initialize PB8 when LCDCONTR0 is active.

4. **Keep the emulator framebuffer one-bit and transport contrast as separate presentation state.** The native_sim EF800 Application contrast service publishes enabled/disabled state and duty metadata through the virtual front-panel path. The gateway forwards it independently of LCD pixel frames; the browser derives foreground/background shades, treating 78% as the current normal-contrast reference, lower values as lower contrast, and higher values as higher contrast. LCDCONTR0 retains the current fixed styling. This preserves ADR-0007's stable binary frame contract and the T6963 model's logical pixels.
   - Alternative considered: convert the emulator framebuffer to grayscale or bake contrast into pixels. Rejected because it conflates LCD content with analog presentation and breaks the packed one-bit contract.

5. **Make Settings menu structure conditional and maintain stable navigation.** Build the Settings list from the active composition so its item count, shortcuts, selection, and activation all agree. Add the contrast editor only in LCDCONTR1; accepted duty edits apply immediately and persist. Add shell commands to get/set duty and to stage/commit the LCDCONTR composition selection; mode changes report restart-required and never reconfigure PB8 during the current boot. A custom Configuration menu remains out of scope.

## Risks / Trade-offs

- [The Zephyr STM32 PWM driver's initialization-time pinctrl could mux PB8 before the active composition is read] -> Keep PB8 out of unconditional PWM pinctrl and prove its disabled-state electrical configuration in a target build; use the timer/pin wrapper described above if necessary.
- [The 40 kHz filtered output may not produce the expected voltage or ripple on the modified PCB] -> Verify 40 kHz, duty, and filtered voltage with a scope/meter on the modified unit; retain the hardware qualification gate and do not infer physical performance from native_sim.
- [A malformed or unwritable contrast file could prevent contrast setup] -> Validate the percentage before PWM use, fall back to 78% with a diagnostic, avoid formatting or replacing unrelated Configuration data, and report persistence errors without blocking application startup.
- [Settings arrays and keyboard navigation assume fixed item indexes] -> Test menu count, shortcuts, entry activation, return navigation, and live appearance/disappearance after Configuration state changes.
- [EF800 Backup flash budget is constrained] -> Reuse the shared small service and run the prescribed pristine EF800 Backup build and image-size checks.
- [Native-sim and browser shade mapping is illustrative rather than photometric] -> Keep 78% as a stable reference and treat browser visuals as behavioral feedback, not colorimetric or hardware evidence.

## Migration Plan

No QSPI repartitioning or format is required. If the active composition selects LCDCONTR1 and no duty file exists, first startup creates it at 78%; later edits update that value. LCDCONTR0 leaves any duty file intact and does not initialize PB8. A mode change is staged through COMPOSITION and takes effect after restart; rollback is a committed LCDCONTR0 token.

## Open Questions

- The browser needs the simulator's active LCDCONTR state and duty, but current LCD frame messages contain only pixels and text. The implementation must add a separate bounded presentation-state message/API while preserving existing LCD frame payload compatibility.
