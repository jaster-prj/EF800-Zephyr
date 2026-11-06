## MODIFIED Requirements

### Requirement: Present the EF800 front panel in a browser
The web front panel SHALL render the complete 240 x 64 monochrome display,
expose the 59 canonical EF800 keys in a keyboard-like layout, and communicate
live state and input through a bounded WebSocket interface. When LCD contrast
control is active, the browser SHALL render the LCD's foreground/background
contrast according to the current persisted duty value, using 78% as the
normal-contrast reference. The browser SHALL retain the existing fixed LCD
appearance while contrast control is disabled. Contrast presentation SHALL
NOT modify the emulator's logical one-bit framebuffer. The browser SHALL not
present duplicate aliases such as `return` in addition to `enter`, or unused
raw matrix positions as actionable controls.

#### Scenario: Display frame arrives from the simulator
- **WHEN** the gateway accepts a complete valid LCD frame
- **THEN** it retains that frame and broadcasts its sequence, pixels, and available logical text state to connected browsers

#### Scenario: Contrast-enabled LCD is presented at the normal reference
- **WHEN** the simulator reports active LCD contrast control with a duty value of 78%
- **THEN** the browser renders the logical LCD pixels with its normal-contrast reference appearance without changing the one-bit framebuffer

#### Scenario: Lower duty reduces browser LCD contrast
- **WHEN** the simulator reports active LCD contrast control with a duty value below 78%
- **THEN** the browser reduces the visual difference between LCD foreground and background shades in accordance with the reported value

#### Scenario: Higher duty increases browser LCD contrast
- **WHEN** the simulator reports active LCD contrast control with a duty value above 78%
- **THEN** the browser increases the visual difference between LCD foreground and background shades in accordance with the reported value

#### Scenario: Contrast control is disabled in the simulator
- **WHEN** the simulator reports `LCDCONTR0`
- **THEN** the browser retains the existing fixed-contrast LCD appearance

#### Scenario: User operates the browser keypad
- **WHEN** the controlling browser presses, holds, and releases a supported
  canonical key
- **THEN** the gateway sends distinct semantic key-down and key-up operations
  through the normal simulator keyboard path and the browser marks the key as
  pressed only while it is held

#### Scenario: Browser renders the full key inventory
- **WHEN** a user opens the front panel
- **THEN** the page presents 59 actionable canonical key controls, including
  text, editing, navigation, power, and modifier keys, in a keyboard-like
  arrangement
