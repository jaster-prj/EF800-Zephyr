## Why

The Virtual EF800 browser currently exposes only six navigation controls even
though the simulator's existing keyboard resolver supports the complete EF800
keyboard matrix. This prevents browser-driven testing of text entry, editing,
modifiers, power controls, and realistic multi-key interactions.

## What Changes

- Expand the browser front panel from the six navigation controls to the 59
  unique user-facing keys supported by the EF800 keyboard map: 54 matrix keys
  and 5 special keys.
- Present the controls in a keyboard-like layout without duplicate aliases or
  unused raw matrix positions.
- Permit the expanded canonical key names through the browser gateway while
  preserving the existing simulator keyboard and scancode path.
- Preserve held-key behavior for pointer/touch and keyboard input, including
  simultaneous held keys for modifier combinations.
- Mark held keys with an inverted visual state and an accessible pressed state.
- Release held keys and clear their visual state on key release, focus loss,
  control release, disconnect, or release-all.
- Add coverage for the expanded key inventory, gateway validation, held-key
  behavior, and pressed-state browser assets.

## Capabilities

### New Capabilities

<!-- None. The change extends the existing Virtual EF800 platform capability. -->

### Modified Capabilities

- `virtual-ef800-platform`: Expand the browser keypad contract from the
  current six semantic controls to the complete canonical 59-key user-facing
  set, including held-key and visual-state behavior.

## Impact

- `GSE/Virtual-EF800/web/index.html`, `app.js`, and `style.css` for the
  browser keyboard layout, interaction state, accessibility state, and
  pressed styling.
- `GSE/Virtual-EF800/gateway.py` for canonical key validation and forwarding.
- Virtual EF800 web and gateway tests for key inventory and lifecycle behavior.
- No changes to the underlying matrix-to-scancode resolver or simulator wire
  protocol are expected; the existing virtual keyboard path remains the source
  of keyboard translation.
