## Why

The modified EF800 hardware can adjust the LCD contrast through a filtered PWM signal, while unmodified units must retain their static contrast. There is currently no runtime contrast control or simulator feedback, so users cannot select a comfortable contrast or verify the setting in the native_sim front panel.

## What Changes

- Add LCD contrast control selected by the `LCDCONTR0` / `LCDCONTR1` target-composition token. `LCDCONTR1` enables PWM contrast; `LCDCONTR0` leaves contrast static and does not initialize PB8. A committed mode change takes effect after restart, following the COMPOSITION contract.
- Generate 40 kHz hardware PWM on PB8/TIM4_CH3 on enabled units in EF800 Application and EF800 Backup. The modified PCB connects the MCU-side JP15 pad by flying wire; JP15 remains open.
- Persist the duty in a separate contrast file under `.configuration`. Create it at 78% when contrast control is active and no saved value exists; load and retain an existing value, including while control is disabled.
- Add a contrast entry to EF800 Application Settings only while the active composition is `LCDCONTR1`, and provide console duty controls. Add a console composition change for LCDCONTR0/1 that reports restart-required state; a custom Configuration menu remains out of scope.
- Make the native_sim browser LCD reflect the configured contrast level, using 78% as the normal-contrast reference, without changing the emulator's logical one-bit framebuffer.

## Capabilities

### New Capabilities
- `lcd-contrast-control`: Runtime configuration, PWM output, persistence, EF800 Application Settings and console behavior, and corresponding Main/Backup startup behavior.

### Modified Capabilities
- `virtual-ef800-platform`: Browser LCD presentation reflects the active contrast setting in native_sim.
- `target-composition-profile`: The LCDCONTR0/LCDCONTR1 mode is selected through the existing restart-activated composition profile.

## Impact

- EF800 Application Settings, console commands, Configuration-backed persistence, and startup composition.
- EF800 Backup startup and access to the shared Configuration FAT volume.
- EF800 board DeviceTree/pinctrl and Zephyr PWM configuration for PB8/TIM4_CH3.
- Native_sim contrast state propagation and the `GSE/Virtual-EF800` browser LCD renderer and tests.
- New capability spec plus a delta to the existing `virtual-ef800-platform` capability; no change to the T6963 logical framebuffer contract.
