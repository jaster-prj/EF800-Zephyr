## Context

The Virtual EF800 gateway currently validates and renders only six keys:
`up`, `down`, `left`, `right`, `enter`, and `esc`. The native-sim virtual
keyboard already resolves names through the shared EF800 keyboard map, which
contains 54 unique occupied matrix positions and 5 special keys. The browser
therefore has a narrower input surface than the simulator and cannot exercise
normal text entry, editing, modifiers, power controls, or multi-key behavior.

The existing browser already tracks held keys, sends distinct key-down and
key-up messages, and sends release-all on focus loss and unload. The gateway
already owns controller arbitration and releases held simulator input when the
controller disconnects. The design should extend those existing boundaries,
not introduce a second keyboard protocol or direct application manipulation.

## Goals / Non-Goals

**Goals:**

- Expose the 59 unique canonical keys supported by the existing keyboard map.
- Arrange them as a recognizable keyboard-like browser control surface.
- Preserve physical hold semantics and allow simultaneous keys, especially
  modifier combinations.
- Make held state visible through inverted styling and an accessible pressed
  state.
- Keep gateway validation, browser state, and simulator state consistent during
  rejection, release, focus loss, and disconnect paths.
- Keep the existing simulator binary protocol and matrix-to-scancode resolver
  unchanged.

**Non-Goals:**

- Exposing the two unused positions in the theoretical 8x7 matrix.
- Showing duplicate aliases (`return`, `escape`, `back`, or `control`) as extra
  controls.
- Adding a raw matrix diagnostic view in this change.
- Changing keyboard hardware firmware, scancode translation, or EF800 Application
  input handling.
- Adding browser text-entry synthesis that bypasses the EF800 keyboard path.

## Decisions

### Use the existing canonical keyboard map

The browser and gateway will use one canonical user-facing name per supported
physical key. The key inventory is derived from the existing resolver rather
than duplicated as a new matrix table. This avoids drift between browser
controls and the simulator's scancode implementation.

An alternative would expose all 56 raw matrix coordinates. That would surface
two empty positions, make the UI diagnostic rather than keyboard-like, and
still require labels. Another alternative would retain a manually curated
browser list of only common keys, but that would repeat the current limitation
and make future map changes easy to miss.

### Use momentary held-key semantics

Pointer/touch down and browser key-down produce a held key; the matching
release event produces key-up. Multiple keys are tracked independently. The
gateway remains authoritative for accepted simulator input, while the browser
updates its visual state only for accepted operations.

Modifier keys therefore work through overlapping key-down/key-up operations.
No click-to-latch behavior is introduced; a later change could add a separate
toggle only if a usability need is demonstrated.

### Represent held state with semantic and visual state

Each browser key control will expose its current state through an accessible
pressed attribute and an inverted visual class/state. State clearing will use
the existing release-all lifecycle paths so a stale pressed key is not left on
screen after controller loss or connection failure.

### Keep the gateway as the compatibility boundary

The gateway's canonical allow-list will expand, but the internal simulator
message remains a bounded payload containing the key name. The native virtual
keyboard will continue resolving that name and producing normal make/break
scancodes. This preserves the one gateway-owned simulator connection and the
existing headless client boundary.

## Risks / Trade-offs

- **[Risk]** Browser key inventory can drift from the C keyboard map.  
  **Mitigation:** test the canonical inventory and keep one canonical list per
  boundary, with the shared resolver remaining the final acceptance authority.
- **[Risk]** Pointer cancellation or browser focus transitions can leave a key
  visually pressed.  
  **Mitigation:** centralize release-all cleanup and clear browser state on all
  controller/disconnect/focus lifecycle events.
- **[Risk]** A gateway accepts a key while the simulator rejects it, causing
  state divergence.  
  **Mitigation:** update held state and pressed styling only after the gateway's
  simulator send succeeds; retain existing simulator-side name validation.
- **[Risk]** A full keyboard increases visual density on small screens.  
  **Mitigation:** use a responsive, wrapped keyboard-like layout and keep raw
  matrix diagnostics out of the primary surface.

## Migration Plan

This is a backward-compatible browser and gateway expansion. Existing six-key
clients remain valid because their canonical names remain supported. Build the
updated web gateway image, restart the Compose deployment, and verify the
health endpoint plus browser control acquisition. Rollback consists of
redeploying the previous gateway image; no simulator flash-volume migration or
wire-protocol migration is required.

## Open Questions

- Whether the final visual arrangement should prioritize a conventional
  QWERTY layout or preserve a more literal EF800 physical-key grouping.
- Whether an explicit raw matrix diagnostic view should be proposed separately
  after the keyboard-like surface is validated.
