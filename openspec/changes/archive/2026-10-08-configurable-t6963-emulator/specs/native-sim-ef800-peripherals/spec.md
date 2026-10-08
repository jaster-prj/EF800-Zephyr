## MODIFIED Requirements

### Requirement: Model connected peripheral wiring
The native-sim target SHALL propagate observable output state between emulated devices according to the EF800 wiring, including MCP23017-controlled keyboard reset and LCD control, T6963 parallel-bus transactions, keyboard UART traffic, and scripted measurement-port peers. The LCD wiring SHALL connect production T6963 GPIO transactions to the configurable T6963C model and its incremental packed framebuffer output.

#### Scenario: LCD driver writes a display byte
- **WHEN** the production T6963 driver toggles emulated data and control GPIOs for a write cycle
- **THEN** the virtual T6963 model consumes the cycle, invalidates the affected display region, updates the packed framebuffer when pixels change, and exposes the resulting complete snapshot

#### Scenario: LCD driver reads display data
- **WHEN** the production T6963 driver performs a status or display-data read cycle
- **THEN** the virtual T6963 model drives the configured status or RAM value onto the emulated data pins and applies the command's read-address semantics

#### Scenario: Keyboard reset is released
- **WHEN** the production application changes the emulated MCP23017 keyboard-reset output to the released state
- **THEN** the virtual keyboard controller emits its configured startup response through the emulated keyboard UART
