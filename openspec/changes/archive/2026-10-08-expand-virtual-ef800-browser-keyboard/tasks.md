## 1. Canonical Key Inventory and Gateway Contract

- [x] 1.1 Define the 59 canonical browser key names from the existing EF800 keyboard map, excluding aliases and unused matrix positions.
- [x] 1.2 Expand gateway key validation to accept the canonical inventory while retaining controller ownership checks and bounded simulator payloads.
- [x] 1.3 Add gateway tests covering acceptance of representative text, editing, modifier, power, and navigation keys, plus rejection of unknown names.
- [x] 1.4 Add gateway lifecycle tests verifying held-key tracking and clearing after release-all, controller release, and disconnect cleanup.

## 2. Browser Keyboard Surface

- [x] 2.1 Replace the six-key browser keypad markup with a keyboard-like layout containing exactly 59 actionable canonical controls.
- [x] 2.2 Use one visible canonical label per physical key and do not render duplicate aliases or unused raw matrix positions.
- [x] 2.3 Add responsive styling appropriate for the full keyboard surface while preserving touch-safe controls and the existing front-panel layout.
- [x] 2.4 Add inverted held-key styling and accessible pressed-state attributes for every browser key.

## 3. Held-Key Interaction State

- [x] 3.1 Route pointer, touch, and browser keyboard down/up events through shared key-state handling for simultaneous keys and modifiers.
- [x] 3.2 Update browser held state and pressed styling only after accepted key operations, and clear state on release, cancellation, focus loss, controller release, disconnect, and release-all.
- [x] 3.3 Preserve the existing six-key browser and physical keyboard mappings as compatible aliases where they correspond to canonical controls.
- [x] 3.4 Add browser asset tests for the complete key inventory, held-state behavior, inverted styling, and accessible pressed state.

## 4. Verification and Deployment

- [x] 4.1 Run the Virtual EF800 gateway and web asset test suites, including multi-key and cleanup scenarios.
- [x] 4.2 Build the native-sim `ef800-application` artifact and rebuild the Compose web gateway image without changing the simulator protocol.
- [x] 4.3 Start the Compose deployment and verify health, controller acquisition, representative key make/break behavior, and held-key visual state manually or through a browser test.
- [x] 4.4 Confirm existing native-sim behavioral and deployment diagnostics remain functional and document any unresolved layout decision.
