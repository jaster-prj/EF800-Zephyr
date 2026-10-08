## ADDED Requirements

### Requirement: Provide configurable T6963C profiles
The T6963C emulator SHALL accept a validated profile for external memory capacity, hardware columns, font width, text rows, text area, graphics area, scan mode, and character-generator mode, while supporting the EF800 default profile of 8 KiB, 40 columns, 6x8 characters, 8 text rows, and a 240 x 64 display.

#### Scenario: Initialize the EF800 profile
- **WHEN** the emulator is initialized with the default EF800 profile
- **THEN** it exposes 8 KiB of addressable display RAM and a 240 x 64 logical display surface with 40 text/graphics columns

#### Scenario: Initialize a larger profile
- **WHEN** the emulator is initialized with a valid 64 KiB and 64-column profile
- **THEN** address pointers and display-area registers operate across the configured 16-bit memory space without applying the EF800 8 KiB address mask

#### Scenario: Reject an invalid profile
- **WHEN** a profile uses an unsupported hardware column count, font width, geometry, or memory capacity
- **THEN** initialization fails without creating a partially valid emulator

### Requirement: Emulate T6963C register and command behavior
The emulator SHALL implement the configured T6963C address, text-home, text-area, graphics-home, graphics-area, offset, mode, display-mode, cursor, auto-read/write, data read/write, screen operation, and bit set/reset command behavior needed by the production driver and conformance tests.

#### Scenario: Write and read display RAM with address movement
- **WHEN** a caller sets an address and performs incrementing, decrementing, or fixed-address data operations
- **THEN** the emulator reads or writes the selected RAM byte and updates the address pointer according to the command

#### Scenario: Transfer data in auto mode
- **WHEN** the caller enters auto-read or auto-write after setting an address
- **THEN** successive data cycles transfer bytes using the configured automatic address progression until auto reset

#### Scenario: Set or reset an addressed bit
- **WHEN** the caller sends a T6963C bit set or bit reset command
- **THEN** only the selected bit of the addressed RAM byte changes and the display region containing that byte becomes dirty

### Requirement: Render text and graphics into a packed one-bit framebuffer
The emulator SHALL derive display pixels from text RAM, graphics RAM, character-generator data, display registers, composition mode, attributes, cursor state, and blink phase, and SHALL store the resulting framebuffer with exactly one bit per pixel.

#### Scenario: Render a changed graphics byte
- **WHEN** a graphics RAM byte changes
- **THEN** the emulator recomputes only the affected packed framebuffer pixels using the configured font width and graphics-area stride

#### Scenario: Render a changed text cell
- **WHEN** a text RAM character changes
- **THEN** the emulator recomputes the corresponding character cell from the active internal or external character generator without using a byte-per-pixel intermediate framebuffer

#### Scenario: Compose text and graphics
- **WHEN** text and graphics are enabled with OR, EXOR, AND, or attribute mode
- **THEN** the framebuffer contains the corresponding logical composition rather than whichever plane was rendered last

### Requirement: Update rendering incrementally
The emulator SHALL track dirty display regions caused by RAM or display-state changes and SHALL update the packed framebuffer only for affected regions; changes that cannot be localized SHALL invalidate the full configured display surface.

#### Scenario: Change a character-generator row
- **WHEN** a character-generator RAM byte changes
- **THEN** all visible text cells using the affected character are invalidated and refreshed, while unrelated cells remain untouched

#### Scenario: Move the cursor
- **WHEN** the cursor pointer or cursor visibility changes
- **THEN** both the previous and new cursor regions are refreshed so no stale cursor pixels remain

#### Scenario: Change a non-display register
- **WHEN** only the address pointer changes
- **THEN** no framebuffer region is invalidated because the address pointer alone does not alter displayed pixels

### Requirement: Publish stable framebuffer snapshots
The emulator integration SHALL retain the existing complete-frame transport format for the EF800 profile and SHALL publish a frame only after dirty-region rendering changes at least one framebuffer bit.

#### Scenario: Dirty rendering changes pixels
- **WHEN** a display operation changes one or more packed framebuffer bits
- **THEN** the integration publishes one complete, consistently framed 240 x 64 snapshot

#### Scenario: Dirty rendering produces no visual change
- **WHEN** a register or RAM operation is repeated but the final pixels are unchanged
- **THEN** no redundant framebuffer update is published

#### Scenario: Client receives a partial snapshot
- **WHEN** a client receives an incomplete or malformed framebuffer message
- **THEN** it rejects the message and retains the previous complete snapshot
