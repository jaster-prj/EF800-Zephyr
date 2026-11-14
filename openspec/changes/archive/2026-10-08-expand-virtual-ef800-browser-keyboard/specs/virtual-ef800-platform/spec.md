## MODIFIED Requirements

### Requirement: Accept virtual keypad press and release input
The virtual EF800 SHALL expose the complete canonical EF800 user-facing key
set through distinct press and release operations. The set SHALL contain the
59 unique keys represented by the existing keyboard map: 54 occupied matrix
positions and 5 special keys. Duplicate aliases and unused raw matrix
positions SHALL NOT be exposed as separate user-facing controls. Accepted
input SHALL continue through the normal EF800 Application keyboard translation,
parser, and application-handling path rather than directly mutating menu or
plugin state.

The browser front panel SHALL preserve held-key state for pointer/touch and
keyboard input, SHALL permit multiple simultaneous held keys, and SHALL expose
held state through an inverted visual treatment and an accessible pressed
state. Key state SHALL be released on the corresponding release event or by
release-all when control is released, the browser loses focus, the controller
disconnects, or the simulator connection is lost.

#### Scenario: User operates a canonical browser key
- **WHEN** the controlling user presses and releases any one of the 59
  canonical browser keys
- **THEN** the gateway sends the corresponding key-down and key-up operations
  through the normal simulator keyboard path and EF800 Application receives the
  resulting make and break behavior

#### Scenario: User holds a virtual key
- **WHEN** the controlling user presses a browser key without releasing it
- **THEN** the key remains logically held, its browser control is rendered with
  inverted pressed styling and an accessible pressed state, and supported
  repeat behavior follows the virtual keyboard policy

#### Scenario: User holds multiple keys
- **WHEN** the controlling user holds a modifier and another canonical key at
  the same time
- **THEN** both keys remain held independently and the simulator receives the
  corresponding overlapping key-down operations until each key is released

#### Scenario: Front panel loses control with held keys
- **WHEN** the active controller releases control, loses browser focus, closes,
  disconnects, or loses the simulator connection while keys are held
- **THEN** the gateway sends release-all, clears its held-key set, and the
  browser clears all pressed styling and accessible pressed states

### Requirement: Present the EF800 front panel in a browser
The web front panel SHALL render the complete 240 x 64 monochrome display,
expose the 59 canonical EF800 keys in a keyboard-like layout, and communicate
live state and input through a bounded WebSocket interface. The browser SHALL
not present duplicate aliases such as `return` in addition to `enter`, or
unused raw matrix positions as actionable controls.

#### Scenario: Display frame arrives from the simulator
- **WHEN** the gateway accepts a complete valid LCD frame
- **THEN** it retains that frame and broadcasts its sequence, pixels, and
  available logical text state to connected browsers

#### Scenario: User operates the browser keyboard
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
