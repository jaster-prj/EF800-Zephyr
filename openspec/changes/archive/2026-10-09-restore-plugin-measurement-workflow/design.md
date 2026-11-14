## Context

The current EF800 Application has a plugin instrument menu and a generic measurement editor, but they are joined in the wrong order. A plugin measurement is executed before point metadata is collected. After the result is returned, the display can show the legacy host actions while activation still dispatches through the plugin action bitmap. Fennel1 declares only `Measure`, so its pending result cannot reach staff-height, save, discard, or fixed-measurement actions. Elta3's instrument actions are likewise being confused with post-measurement actions.

The legacy Mbed workflow collected point code and point number before the instrument request, then used a common result menu for staff height, save, discard, and fixed measurement. The desired architecture places that complete workflow inside each measurement plugin. The workspace service remains host-owned, but owns only the current generic point context and opaque line append. The design must preserve the package-owned instrument boundary from ADR-0001 and ADR-0010 without making EF800 Application understand measurement actions or record fields.

## Goals / Non-Goals

**Goals:**

- Keep the complete measurement lifecycle inside each plugin, including code, point number, staff height, save, discard, and fixed measurement.
- Pass the workspace's current point number and code into a plugin session when it opens.
- Keep Elta3 `Up`, `Down`, and `Enter` as plugin instrument actions and keep Fennel1 measurement-only.
- Return a bounded serialized workspace line from the plugin when it saves a result.
- Make plugin-owned `Fixmessung` save, increment the workspace point number, trigger the next measurement, and return to the plugin's pending-result actions.
- Let the workspace object extract and retain current point number/code on load and update that context after appends.
- Add native-sim and unit-level coverage for the complete workflow and repeated fixed measurements.

**Non-Goals:**

- Changing the workspace file format or moving persistence into plugin packages.
- Adding directional commands to Fennel1.
- Adding instrument-specific branches for Elta3 or Fennel1 to EF800 Application.
- Changing plugin ABI ownership of instrument actions, bounded display models, or host services.
- Reworking port assignment, UART ownership, or plugin discovery.

## Decisions

### Keep complete workflow state in plugins

Each plugin will distinguish its instrument and pending-result states internally. EF800 Application will only distinguish whether a generic plugin session is active and whether a plugin has returned a serialized line for the workspace service:

```text
PLUGIN_SESSION
    |
    +-- generic key/input --> plugin-owned workflow
                              |
                              +--> serialized workspace line
                                           |
                                           v
                                   host workspace append
```

The host will render the plugin's bounded display model and forward generic logical input. It will not display measurement-specific action labels or use plugin action indices to dispatch measurement behavior.

### Keep workflow behavior in plugins and workspace context in the host

The plugin session is responsible for the complete measurement transaction, display, logical input, point metadata, pending result, save/discard, fixed measurement, and serialized record generation. EF800 Application supplies a generic session context and host services, then appends the returned opaque line. Plugins receive no direct filesystem or workspace object pointer.

### Collect metadata before measurement

When a plugin session opens, EF800 Application supplies the current code and point number held by the workspace object. The plugin uses those values as defaults and owns editing and validation. The plugin measurement runs only after its own workflow accepts the required metadata. Measurement failures will not produce a serialized line or workspace append.

### Make fixed measurement advance the point

`Fixmessung` will ask the host to append the plugin's serialized current result, then use the updated workspace point context, increment the point number, and trigger the next plugin measurement. The code and current staff-height value remain the plugin's defaults for the next cycle. Discard remains entirely inside the plugin and does not cross the host boundary.

### Preserve one active port-bound plugin session

The existing active plugin session and transport remain in use across the complete plugin workflow. The host will not open a second UART claim for any workflow action. Session teardown remains host-controlled when the user cancels or leaves the workflow.

### Test at the workflow boundary

Tests will verify workspace context extraction/update, ABI context initialization, plugin-owned workflows, bounded serialized output, opaque workspace append, fixed-measurement point increments, Elta3 directional actions, Fennel1 unsupported directions, and session cleanup. Native-sim coverage will verify the observable `workspace.dat` record after Save and after Fixmessung.

## Risks / Trade-offs

- **[Risk]** A plugin can return an invalid or oversized workspace line. -> Validate bounded output and record syntax at the generic workspace append boundary without interpreting instrument-specific fields in EF800 Application.
- **[Risk]** Point numbers can diverge from existing workspace contents after reboot. -> Extract the last relevant measurement context during workspace load and update it after successful append.
- **[Risk]** Fixed measurement can append duplicate records if incrementing occurs at the wrong transition. -> Have the workspace context provide the next point number and assert strictly increasing values in repeated fixed-measurement tests.
- **[Risk]** Native-sim protocol success may not prove persistence success. -> Assert both plugin workflow output and the resulting opaque workspace line.

## Migration Plan

1. Extend the workspace object with current measurement context extraction/update.
2. Extend the plugin ABI and host session wrapper with context initialization and bounded serialized output.
3. Move the complete measurement workflow into Fennel1 and Elta3.
4. Reduce EF800 Application integration to generic context transfer and opaque workspace append.
5. Add unit, plugin-host, and native-sim workflow tests, including repeated `Fixmessung`.
6. Run the required firmware builds and native test suite; no persisted-data migration is expected.

Rollback consists of restoring the previous EF800 Application image and plugin package set together. Existing workspace files remain readable because their format is unchanged.

## Open Questions

- The exact source used to initialize the first point number after reboot should remain covered by the existing application behavior; if persisted-number recovery is required, it should be specified as a separate change rather than coupled to this workflow repair.
