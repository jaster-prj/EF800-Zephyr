# Use a Configurable Packed T6963 Renderer

- Status: accepted
- Date: 2026-10-08
- Supersedes: None

## Context

The EF800 native-sim and standalone TLX display paths share a T6963C behavioral model. The initial model is fixed to the EF800's 8 KiB memory and 240 x 64 display and recomputes or publishes display state without representing the broader T6963C register and geometry model. Future display tests and compatible panels need configurable memory and geometry, while the EF800 front-panel contract requires a packed one-bit framebuffer and complete stable snapshots.

Using a byte-per-pixel framebuffer would simplify composition but would contradict the display representation, increase memory use, and make the emulator's output model differ from the monochrome device. Publishing partial updates would reduce transfer size but would add synchronization and recovery complexity to the existing front-panel protocol.

## Decision

The shared T6963C emulator will use validated caller-supplied configuration and display RAM. It will support the EF800 profile by default while allowing larger memory and alternate supported geometries for tests and future display targets.

The canonical rendered framebuffer will remain packed at one bit per pixel. Text, graphics, character-generator, attribute, cursor, and mode behavior will be rendered from current register and RAM state into dirty packed regions. The renderer will publish a complete framebuffer snapshot only when at least one framebuffer bit changes; the existing EF800 transport format will remain unchanged.

## Consequences

- Native-sim and standalone display emulation can exercise multiple T6963C profiles without duplicating models.
- The EF800 uses no byte-per-pixel framebuffer and retains its 1,920-byte complete-frame contract.
- Rendering must track invalidation for localized RAM changes and use full invalidation for global display-state changes.
- Caller-owned RAM requires explicit capacity and lifetime validation.
- The model provides behavioral evidence for source-level display workflows, not proof of STM32, electrical, timing, or physical LCD behavior.
