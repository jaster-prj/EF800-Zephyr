## 1. Configurable emulator foundation

- [x] 1.1 Define validated T6963C profile data for memory capacity, hardware columns, font width, rows, scan mode, text/graphics areas, and character-generator mode.
- [x] 1.2 Replace the fixed 8 KiB emulator storage with caller-owned RAM and preserve an EF800 default initialization path for the 8 KiB, 40-column, 6x8, 240 x 64 profile.
- [x] 1.3 Add address-space helpers and profile validation tests for EF800 8 KiB wrapping and full 16-bit 64 KiB profiles.

## 2. T6963C register and command model

- [x] 2.1 Expand register state for cursor pointer, offset, text/graphics homes and areas, mode, display mode, cursor pattern, auto mode, pending reads, and screen-operation status.
- [x] 2.2 Implement incrementing, decrementing, and fixed data read/write operations plus auto-read, auto-write, and auto-reset semantics.
- [x] 2.3 Implement mode/display/cursor commands, bit set/reset commands, screen peek, and screen copy for supported configured profiles.
- [x] 2.4 Implement dynamic status bits and readback behavior for normal, auto-read, auto-write, and screen-operation error states.

## 3. Packed incremental renderer

- [x] 3.1 Add one-bit framebuffer accessors, change detection, and dirty-region tracking without introducing byte-per-pixel framebuffer or plane storage.
- [x] 3.2 Implement text-cell rendering from internal CG ROM and external CG RAM using the configured offset and font width.
- [x] 3.3 Implement graphics-byte rendering using configurable graphic home, graphic area, address stride, and pixel bit order.
- [x] 3.4 Implement OR, EXOR, AND, text-attribute, cursor, and deterministic blink composition from current register/RAM state.
- [x] 3.5 Invalidate and refresh affected regions for text RAM, graphics RAM, character-generator RAM, cursor changes, display-mode changes, and global register changes.
- [x] 3.6 Add golden tests proving single-bit framebuffer behavior, EF800 bit order, text/graphics composition, custom characters, attributes, cursor rendering, and no stale pixels after clearing.

## 4. Native-sim and standalone integration

- [x] 4.1 Update the native-sim T6963 GPIO observer to initialize the configurable model, route reads and writes, and expose complete snapshots after changed rendering.
- [x] 4.2 Update the standalone TLX-711A path to use the same configurable model while retaining its 240 x 64 packed framebuffer transport.
- [x] 4.3 Suppress redundant framebuffer publications when dirty rendering produces no changed framebuffer bit and preserve last-complete-frame behavior for clients.
- [x] 4.4 Add native-sim integration coverage for production T6963 driver writes, reads, clears, custom characters, graphics pixels, and front-panel frame updates.

## 5. Validation and documentation

- [x] 5.1 Run the native keyboard, protocol, RTC, and persistence tests together with T6963 emulator tests and verify the configured EF800 profile.
- [x] 5.2 Build all required firmware targets and confirm the configurable emulator remains excluded from physical firmware paths except where explicitly selected.
- [x] 5.3 Update T6963 emulator and virtual-display documentation with supported profiles, packed framebuffer layout, command coverage, and behavioral-simulation limitations.
- [x] 5.4 Record test results and any unsupported T6963C command/profile limitations in the migration status documentation.
