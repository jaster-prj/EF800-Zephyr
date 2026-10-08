## Context

The current `t6963_emulator` models the EF800 display with fixed 8 KiB RAM, 40 columns, 6x8 characters, and a 240 x 64 output. It supports the common address/control and write paths, but it does not model the full register-driven composition described by the T6963C datasheet. The native-sim GPIO observer and standalone TLX application both consume this shared model, while the virtual front panel requires complete stable framebuffer snapshots.

The T6963C display RAM is byte-addressable, but the observable EF800 display is monochrome: one framebuffer bit represents one pixel. Rendering must therefore preserve a packed one-bit framebuffer and update it incrementally when display RAM or display state changes. Native-sim remains behavioral device emulation, not STM32 register or electrical timing emulation, consistent with ADR-0003, ADR-0005, and ADR-0006.

## Goals / Non-Goals

**Goals:**

- Make memory capacity and display geometry configurable while retaining the EF800 default profile.
- Model the T6963C registers and command groups used by production and conformance tests.
- Derive text, graphics, custom-character, attribute, cursor, blink, and composition output from current register/RAM state.
- Keep the framebuffer packed at one bit per pixel with no byte-per-pixel intermediate surface.
- Track dirty regions and recompute only affected packed framebuffer bytes where possible.
- Preserve complete-frame publication and the existing 240 x 64 transport contract for EF800.
- Share the model between native-sim and the standalone TLX display path.

**Non-Goals:**

- Emulating STM32 registers, ARM execution, LCD electrical timing, scan waveforms, or hardware qualification evidence.
- Changing the browser or simulator transport to partial framebuffer updates.
- Supporting every undocumented T6963C clone variation.
- Replacing production T6963 driver calls with a simulator-only display API.
- Changing the physical EF800 image layout or display wiring.

## Decisions

### Use a validated profile with caller-owned display RAM

The emulator will receive a profile describing memory capacity, hardware columns, font width, rows, scan mode, text/graphics areas, and character-generator mode. The backing RAM will be supplied by the owner so the EF800 path can use 8 KiB while conformance tests can provide 64 KiB without forcing every embedded instance to reserve the maximum.

Alternatives considered:

- A fixed 8 KiB model: rejected because it cannot exercise the T6963C 16-bit address space or alternate panel geometries.
- A fixed internal 64 KiB array: rejected because it wastes RAM for the EF800 standalone target and prevents explicit memory ownership.

### Keep display RAM byte-addressable and the framebuffer bit-packed

Display RAM will retain T6963C byte semantics. The materialized framebuffer will use one bit per pixel, with the existing EF800 row packing and bit order preserved at the output boundary. The renderer will set or clear individual framebuffer bits and compare the previous bit before reporting a visual change.

This avoids a byte-per-pixel cache while still allowing logical composition and incremental updates.

### Render dirty regions from source state

The renderer will not apply irreversible OR, EXOR, or AND operations directly to the framebuffer. It will recompute final pixels from the current text RAM, graphics RAM, character-generator RAM, registers, attributes, cursor, and blink phase for each dirty region.

Localized invalidation will be used for graphics bytes, text cells, cursor movement, and custom-character changes. Register changes affecting addressing, geometry, mode, display selection, or attributes will invalidate the relevant plane or the full display. A custom-character write may scan the visible text area to find all cells using that character; the EF800's 320 cells make this deterministic and inexpensive.

### Separate logical planes conceptually without storing byte-per-pixel planes

Text and graphics will be treated as logical sources during rendering, not as byte-per-pixel buffers. For each dirty pixel, the renderer will obtain the text and graphics bits, apply OR/EXOR/AND or attribute rules, then apply cursor and blink behavior before writing the packed output bit.

This keeps memory use low and ensures that clearing or changing one source correctly reveals the other source.

### Preserve complete-frame transport

Dirty rendering is an internal optimization and correctness boundary. Once rendering completes, the native-sim/front-panel integration will continue to publish a complete 1,920-byte EF800 framebuffer snapshot. It will publish only when at least one framebuffer bit changed. This preserves client recovery from missed packets and compatibility with the existing framed transport.

### Model command readiness and readback state

The emulator will retain enough controller state to distinguish normal status readiness, auto-read, auto-write, screen-operation errors, and data-read pending state. Read commands will support increment, decrement, and fixed address behavior. Screen peek will return logical displayed data where valid, while screen copy and bit operations will update RAM and dirty regions according to the configured geometry.

## Risks / Trade-offs

- [A larger configurable model increases command and rendering complexity] -> Implement command families in independently testable groups and retain the EF800 profile as a focused integration baseline.
- [Incorrect bit ordering creates visually plausible but wrong output] -> Add golden tests for single-pixel, full-byte, text glyph, custom-character, and mixed-plane patterns using the documented EF800 bit order.
- [Full invalidation can increase native-sim rendering work] -> Keep localized invalidation for RAM writes and use full invalidation only for state changes that genuinely affect the whole display.
- [Caller-owned RAM can have an invalid lifetime] -> Require non-null storage and capacity validation during initialization; keep ownership with the application instance that owns the emulator.
- [T6963 variants differ in undocumented behavior] -> Define the supported command subset explicitly and reject unsupported profiles or commands in tests rather than claiming universal hardware equivalence.
- [Complete-frame publication can still transmit unchanged-looking frames after a broad invalidation] -> Compare packed framebuffer bits during rendering and suppress publication when no bit changed.

## Migration Plan

1. Introduce the profile and caller-owned RAM initialization path while preserving an EF800 convenience/default configuration.
2. Expand command/register state and status/readback behavior.
3. Add packed framebuffer bit helpers and dirty-region tracking.
4. Replace whole-frame recomputation with incremental rendering of changed text, graphics, custom-character, cursor, attribute, and mode regions.
5. Connect the native-sim GPIO observer and standalone TLX application to the configurable model without changing the complete-frame transport format.
6. Add unit and native-sim integration tests, then run the four firmware builds and native test suite required by the repository guide.

Rollback can retain the previous fixed emulator entry points behind the same integration boundary while reverting the new profile and renderer implementation; no persistent framebuffer protocol migration is required.

## Open Questions

- Whether future non-EF800 profiles need a generic framebuffer transport, or whether only the internal emulator should be generic while all current clients remain EF800-specific.
- Whether blink progression should use a virtual clock injection immediately or initially expose only deterministic manual blink-phase control in tests.
