## Why

The plugin-owned Measurement menu currently triggers an instrument result before the host collects all point metadata, and then leaves the pending result attached to the plugin action list. Fennel1 therefore cannot reach the common staff-height, save, discard, and fixed-measurement actions, while Elta3 does not consistently preserve the legacy workflow. Both measurement plugins need the same host-owned point lifecycle so successful results can be deliberately saved to `workspace.dat`.

## What Changes

- Restore the complete measurement workflow inside both Fennel1 and Elta3 plugins: enter point code, enter point number, execute measurement, edit staff height, save, discard, and run fixed measurements.
- Extend the workspace object to retain the current measurement point number and code extracted while loading workspace data, and update that context after each appended measurement line.
- Extend the plugin ABI only with generic measurement context initialization and bounded serialized-record output.
- Keep EF800 Application responsible for workspace loading, context transfer, opaque line append, port ownership, and global navigation; it SHALL not own measurement action labels or sequencing.
- Make plugin-owned `Fixmessung` serialize and append the current result, auto-increment the workspace point number, and trigger the next measurement.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `measurement-plugin-submenus`: Separate plugin instrument actions from the common host-owned pending-measurement workflow and define the complete measurement lifecycle for Fennel1 and Elta3.
- `measurement-plugin-owned-ui`: Clarify that plugins provide measurement/instrument behavior while EF800 Application owns point metadata, pending-result editing, save/discard, fixed measurement, and workspace persistence.

## Impact

- `shared/ef800_persistence/ef800_workspace.*` current measurement context extraction and update-on-append behavior.
- Measurement plugin ABI, host session wrapper, and runtime service boundary for context input and serialized output.
- `application/ef800-application/src/main.c` only for generic workspace context transfer and opaque plugin-record append handling.
- Elta3/Fennel1 plugin workflow state, display, input, save/discard, and fixed-measurement behavior.
- Native-sim and shared measurement/plugin tests, including workspace context and repeated fixed-measurement scenarios.
- Existing measurement submenu and plugin-owned UI specifications receive requirement deltas; no workspace file-format migration is intended.
