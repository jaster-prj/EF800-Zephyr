## ADDED Requirements

### Requirement: Select contrast control from Configuration state
EF800 Application and EF800 Backup SHALL treat the active COMPOSITION token `LCDCONTR1` as enabling LCD contrast PWM and `LCDCONTR0` as selecting static contrast. `LCDCONTR0` SHALL be the catalog default. The mode SHALL be read from the active target-composition profile at application startup; it SHALL NOT be stored in a separate contrast-mode file. When `LCDCONTR0` is active, neither application SHALL initialize or drive PB8 for contrast PWM, even if a saved contrast-duty value exists. Composition changes SHALL remain staged and become active only after restart.

#### Scenario: EF800 Application starts with contrast control enabled
- **WHEN** EF800 Application starts with `LCDCONTR1` active
- **THEN** it loads or initializes the saved contrast value and starts contrast PWM on PB8

#### Scenario: EF800 Backup starts with contrast control enabled
- **WHEN** EF800 Backup starts with `LCDCONTR1` active
- **THEN** it loads or initializes the shared contrast value and starts contrast PWM on PB8

#### Scenario: Application starts with contrast control disabled
- **WHEN** either application starts with `LCDCONTR0` active
- **THEN** it leaves the LCD contrast at its static hardware setting and does not initialize PB8 for PWM, regardless of whether a contrast file exists

#### Scenario: Composition defaults to static contrast
- **WHEN** either application starts without an explicit LCD contrast token in the active composition
- **THEN** the catalog-resolved `LCDCONTR0` default leaves static contrast unchanged and PB8 is not initialized for PWM

#### Scenario: Composition change is staged until restart
- **WHEN** EF800 Application stages and commits a change between `LCDCONTR0` and `LCDCONTR1`
- **THEN** the current boot retains its active contrast mode and reports that restart is required

#### Scenario: Restart activates the selected contrast mode
- **WHEN** either application starts after a valid LCD contrast composition change was committed
- **THEN** it resolves the new active token and initializes or omits PWM according to that token

### Requirement: Generate filtered contrast PWM on the modified hardware
When `LCDCONTR1` is active, EF800 Application and EF800 Backup SHALL generate hardware PWM at 40 kHz using PB8/TIM4_CH3. The implementation SHALL leave JP15 open; the modified board's flying wire connects the MCU-side JP15 pad to the contrast filter. Unmodified units SHALL remain unaffected when `LCDCONTR0` is active.

#### Scenario: Initialize the selected PWM output
- **WHEN** contrast control is enabled on the modified EF800 hardware
- **THEN** the application outputs the configured duty cycle at 40 kHz on PB8/TIM4_CH3

### Requirement: Persist the contrast duty in shared Configuration storage
The applications SHALL store one integer contrast duty value from 0% through 100% in the shared `.configuration` area. On first activation of `LCDCONTR1`, if no contrast value exists, the application SHALL create the contrast file with a default value of 78%. If a valid saved value exists, it SHALL take precedence over the default. A malformed or out-of-range file value SHALL NOT be applied and SHALL fall back to 78% with a diagnostic. Disabling contrast control SHALL NOT delete or reset the saved value.

#### Scenario: First activation creates the default
- **WHEN** `LCDCONTR1` is active and the contrast file does not exist
- **THEN** the application creates it with a 78% duty value and applies that value

#### Scenario: Startup uses the persisted value
- **WHEN** `LCDCONTR1` is active and the contrast file contains a valid duty value
- **THEN** the application applies the persisted value instead of 78%

#### Scenario: Saved value survives disable
- **WHEN** contrast control is disabled while a saved value exists
- **THEN** the file and its value remain available for the next activation

#### Scenario: Invalid persisted duty uses safe default
- **WHEN** `LCDCONTR1` is active and the contrast file contains malformed text or a duty outside 0% through 100%
- **THEN** the application reports the invalid value and applies 78% instead

### Requirement: Adjust contrast through Settings and the console
EF800 Application SHALL expose contrast adjustment in Settings only while the active composition selects `LCDCONTR1`. The contrast control SHALL support runtime duty adjustment and persist accepted values. LCDCONTR mode changes SHALL use the COMPOSITION service through the console, commit as a staged profile change, and report restart-required state. A custom Configuration menu is outside this change.

#### Scenario: Settings menu is rendered with contrast enabled
- **WHEN** EF800 Application displays Settings while `LCDCONTR1` is active
- **THEN** Settings includes an entry for changing LCD contrast

#### Scenario: Settings menu is rendered with contrast disabled
- **WHEN** EF800 Application displays Settings while the active composition selects `LCDCONTR0`
- **THEN** Settings does not include an LCD contrast entry

#### Scenario: User changes contrast in Settings
- **WHEN** the user selects and changes the contrast value while `LCDCONTR1` is active
- **THEN** EF800 Application applies the new PWM duty immediately and persists it

#### Scenario: User changes contrast through the console
- **WHEN** a console command requests a valid contrast value while `LCDCONTR1` is active
- **THEN** EF800 Application applies and persists the new value and reports the resulting setting

#### Scenario: Console requests contrast while disabled
- **WHEN** a console contrast adjustment is requested while `LCDCONTR0` is active
- **THEN** EF800 Application reports that contrast control is disabled and does not initialize or drive PB8

#### Scenario: Console changes the LCD contrast composition
- **WHEN** a console command stages and commits `LCDCONTR0` or `LCDCONTR1`
- **THEN** it uses the COMPOSITION service, preserves unrelated tokens, and reports that the selected mode becomes active after restart
