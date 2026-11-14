## ADDED Requirements

### Requirement: Select LCD contrast hardware mode through composition
The target-composition catalog SHALL provide mutually exclusive `LCDCONTR0` and `LCDCONTR1` selections in the `LCDCONTR` group, with `LCDCONTR0` as the supported default for physical EF800 and native_sim. EF800 Application and EF800 Backup SHALL use the active composition selection at startup to decide whether LCD contrast PWM is enabled. A staged or committed profile change SHALL NOT alter the active contrast mode before restart.

#### Scenario: No LCD contrast token is stored
- **WHEN** an application resolves an active composition with no recognized token in the `LCDCONTR` group
- **THEN** it selects the `LCDCONTR0` default and retains static contrast without initializing PB8 for PWM

#### Scenario: LCDCONTR1 is active on startup
- **WHEN** EF800 Application or EF800 Backup starts with `LCDCONTR1` in its active composition
- **THEN** it enables contrast PWM using the separate persisted duty value

#### Scenario: LCDCONTR0 is active on startup
- **WHEN** EF800 Application or EF800 Backup starts with `LCDCONTR0` in its active composition
- **THEN** it leaves PB8 uninitialized for PWM even when a contrast-duty file exists

#### Scenario: LCD contrast composition change is committed while running
- **WHEN** EF800 Application commits a composition change from `LCDCONTR0` to `LCDCONTR1` or vice versa
- **THEN** the active contrast mode remains unchanged and the application reports that restart is required

#### Scenario: Restart activates the committed LCD contrast selection
- **WHEN** an application initializes after a valid LCD contrast composition change was committed
- **THEN** the newly active token determines whether PWM is initialized
