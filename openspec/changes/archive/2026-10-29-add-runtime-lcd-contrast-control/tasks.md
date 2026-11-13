## 1. Shared Contrast State and Persistence

- [x] 1.1 Add a shared contrast-control service used by EF800Application and BackupApplication, initialized from the active LCDCONTR composition selection and reporting active mode/duty to runtime consumers.
- [x] 1.2 Implement `/EF800:/.configuration/contrast` load/save handling for integer duty values 0–100; create 78% on first enabled use, use 78% with a diagnostic for malformed/out-of-range data, and retain the file when disabled.
- [x] 1.3 Add native tests for missing, valid, malformed, and out-of-range values; verify save/load behavior and that disabling does not erase the stored setting.

## 2. Hardware PWM Integration

- [x] 2.1 Configure the EF800 board's TIM4_CH3 PWM capability on PB8 for a 40 kHz carrier and confirm the 80 MHz timer clock produces 2,000 period counts and an exact 1,560-count pulse at 78%.
- [x] 2.2 Implement deferred PB8 alternate-function activation and release so `LCDCONTR0` never muxes or drives PB8; verify the Zephyr STM32 PWM driver's initialization-time pinctrl behavior and use a runtime timer/pin wrapper if static pinctrl would violate this requirement.
- [x] 2.3 Verify the modified-board route uses the MCU-side JP15 pad with JP15 left open; do not alter the keyboard-controller C0 route.

## 3. Application Startup and Runtime Controls

- [x] 3.1 Integrate the shared service into EF800Application and BackupApplication startup so both resolve the same active composition and load the shared duty, defaulting the LCDCONTR group to `LCDCONTR0`.
- [x] 3.2 Add a console command to stage/commit the mutually exclusive LCDCONTR composition selection, preserve unrelated tokens, and report restart-required state without changing the current boot's output.
- [x] 3.3 Add a conditional LCD contrast entry and editor to EF800Application Settings based on the active composition; keep item count, shortcuts, activation, and navigation correct.
- [x] 3.4 Add console contrast get/set controls with 0–100 validation, immediate application and persistence while enabled, and an explicit disabled response without touching PB8 while LCDCONTR0 is active.
- [x] 3.5 Add focused tests for COMPOSITION default and restart activation, enabled/disabled startup, menu visibility/navigation, duty-command validation, and persistence failures that must not prevent application startup.

## 4. Native-Sim and Browser Presentation

- [x] 4.1 Publish LCDCONTR enabled state and duty as separate bounded native-sim front-panel metadata without changing the existing LCD frame payload or one-bit framebuffer.
- [x] 4.2 Forward contrast metadata through the gateway and render browser foreground/background shades with 78% as the normal reference, lower contrast below 78%, higher contrast above 78%, and the current fixed appearance under LCDCONTR0.
- [x] 4.3 Add gateway/browser tests for default, persisted, lower, higher, disabled, reconnect, and malformed contrast metadata while retaining the existing complete-frame validation behavior.

## 5. Verification and Hardware Qualification

- [x] 5.1 Run the prescribed pristine builds for EF800Application, BackupApplication, Bootloader, keyboard controller, and keyboard bootloader; verify image sizes and image contracts.
- [x] 5.2 Run the prescribed native_sim Twister suite and the `GSE/Virtual-EF800` Python tests; update `docs/migration-status.md` if the test baseline changes.
- [ ] 5.3 On modified hardware, verify PB8 remains unconfigured under LCDCONTR0, produces 40 kHz PWM under LCDCONTR1, and yields expected filtered contrast voltage at 78% and at representative user settings; confirm static contrast remains unchanged on unmodified hardware.
