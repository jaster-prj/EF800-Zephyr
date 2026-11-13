## Context

The current EF800Application has a fixed `APPLICATION_MENU_MEASUREMENT_ACTIONS` list and dispatches `Measure` and directional input through separate host functions. The measurement plugin ABI already provides `measure()` and `handle_input()`, but the host activates and destroys an instance for each call. The current plugin sources expose Elta3 directional commands while the second source is named `fts3xx` and rejects directional input; the requested product name is Fennel1 and must be confirmed or mapped during implementation.

The existing UI owns the LCD header, menu rendering, keyboard decoding, port assignment, measurement records, and host navigation. The design must add persistent plugin submenu behavior without giving plugins direct access to raw UART framing, LCD drivers, or global application state.

## Goals / Non-Goals

**Goals:**

- Represent plugin submenu actions as bounded metadata associated with the selected measurement plugin.
- Keep one active plugin session tied to the selected assigned port and route.
- Route logical keyboard events through an explicit plugin state machine with host global-key precedence.
- Preserve existing measurement record/save/discard behavior and the legacy non-plugin path.
- Test the exact Elta3 and Fennel1 menu contracts and session teardown behavior.

**Non-Goals:**

- Redesigning plugin package discovery, ABI loading, USB ownership, or port assignment persistence.
- Allowing plugins to draw directly to the T6963, read raw keyboard frames, create threads, or claim UARTs.
- Adding hot replacement or background filesystem monitoring.
- Assuming that `fts3xx` is Fennel1 without confirming the hardware/product identity.

## Decisions

### Use declarative action metadata plus a host-owned session

Extend the measurement-facing contract or its host-side descriptor adaptation with a bounded action list and action identifiers. The host creates a session object containing the selected plugin ID, port/transport binding, action selection, and plugin lifecycle state. This is preferred over hard-coding plugin IDs in `main.c`, because the menu must be dynamic and future measurement plugins can declare different actions.

### Keep plugin execution behind the existing host boundary

Add a persistent activation/input path to `ef800_plugin_host` rather than calling plugin functions directly from UI code. The session keeps the plugin instance alive while the submenu is active, serializes action calls, and destroys the instance on exit or invalidation. This avoids repeated activation for every key while preserving the existing service-table and port ownership boundary.

### Give host keys precedence and expose logical events only

The keyboard host remains the only raw protocol consumer. After decoding, EF800Application handles global keys and back/cancel first, then offers remaining logical events to the plugin session state machine. A consumed result prevents parent-menu handling; an unconsumed result follows an explicit fallback policy. This prevents a plugin from trapping system navigation or receiving transport-specific bytes.

### Preserve legacy dispatch as a separate branch

The existing built-in measurement action path remains available when the selected assignment resolves to a legacy device adapter. Plugin sessions are created only for plugin-backed entries. This minimizes regression risk and makes the new behavior testable without rewriting unrelated workspace and persistence flows.

### Resolve Fennel1 identity before implementation

The plan will treat the requested Fennel1 action contract as authoritative, but implementation must verify whether the repository's `fts3xx` adapter is the Fennel1 device or whether a new adapter/descriptor is required. The menu contract is independent of the internal source filename; product-visible naming and protocol behavior must not be changed by assumption.

## Risks / Trade-offs

- [Risk] Keeping a plugin instance active can retain port resources longer than one-shot measurement calls. -> [Mitigation] Tie the session to one host-owned transport, reject concurrent sessions, and destroy it on back, assignment changes, route loss, or application shutdown.
- [Risk] Plugin keyboard handling could swallow global navigation. -> [Mitigation] Define global-key precedence in the host dispatcher and test it before plugin dispatch.
- [Risk] A plugin can declare actions that its implementation cannot execute. -> [Mitigation] Validate action metadata against supported callbacks/capabilities and show only valid actions; return a diagnosable error for runtime failures.
- [Risk] The Fennel1/fts3xx naming mismatch may conceal a protocol mismatch. -> [Mitigation] Make device identity and expected protocol an explicit implementation task and acceptance test.
- [Risk] Replacing the fixed action screen could regress save/discard flows. -> [Mitigation] Keep measurement record handling outside the plugin state machine and add regression tests around pending-result transitions.

## Migration Plan

1. Add the action/session contract and host state-machine APIs while retaining the existing one-shot host APIs for legacy callers.
2. Adapt Elta3 and the confirmed Fennel1 implementation to declare their action sets.
3. Integrate plugin-session creation, rendering, keyboard dispatch, action execution, and teardown into EF800Application.
4. Run native plugin and EF800Application tests, then the required firmware/native test baseline.
5. Roll back by disabling plugin submenu session creation and using the existing fixed host action flow; no persisted data or image layout migration is required.

## Open Questions

- Does the product term Fennel1 refer to the repository's `fts3xx` adapter, or is a distinct protocol/adapter required?
- Which physical/logical key names are the final equivalents of plugin `Up`, `Down`, `Enter`, and host back/cancel in the keyboard event API?
- Should a plugin action failure remain on the same action, show an error row, or return to the submenu root? The implementation should choose one deterministic policy and cover it in tests.
