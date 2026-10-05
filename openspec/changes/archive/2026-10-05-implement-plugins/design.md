## Context

The current PluginManager integration has a host-side measurement submenu and display/session model, while the Elta3 and FTS3XX protocol implementations are already isolated in the measurement-plugin SDK. MainApplication still contains legacy device-specific dependencies and must decide actions and presentation details that belong to the selected package. The target is a package boundary in which Elta3 and Fennel1 are complete measurement implementations, while MainApplication remains a generic host for resources and rendering.

The EF800 display is a fixed T6963 device and the keyboard link has raw framing owned by MainApplication. Packages therefore cannot draw pixels or consume UART bytes directly. The existing bounded screen/session abstractions and host-controlled port service provide the appropriate extension points, but their ABI must be available to externally loaded packages and validated before activation.

There are no repository-level ADRs currently in force. Existing `plugin-integration` and `remove-legacy-device` planning artifacts establish relevant direction: external measurement packages are the runtime source of truth, package discovery is startup-scoped, and host resource ownership must not move into plugins.

## Goals / Non-Goals

**Goals:**

- Make Elta3 and Fennel1 independently buildable and installable `.pmp` measurement packages.
- Move package-specific submenu declarations, logical keyboard actions, measurement state, formatting, and bounded display-model generation into the respective packages.
- Extend and version the PluginManager/EF800 measurement contract for submenu metadata, input dispatch, and display models.
- Keep raw keyboard parsing, global navigation, LCD hardware access, port/UART ownership, assignment policy, and plugin lifecycle in MainApplication.
- Remove the legacy `ef800_device_plugin` production dependency from the host path.
- Preserve explicit bounds, ABI validation, unsupported-action behavior, package diagnostics, and independent package discovery.

**Non-Goals:**

- Direct T6963, GPIO, DeviceTree, Zephyr, or raw keyboard/UART access from packages.
- Hot loading, package replacement during a running session, arbitrary plugin-created threads, or a new package security model.
- Changing measurement wire protocols beyond moving their implementation into package-owned sources.
- Redesigning the persisted assignment format or changing stable Elta3/Fennel1 package IDs without a separate migration decision.

## Decisions

### Use a package-owned measurement session API

The measurement interface will expose package callbacks or a package-owned session API for action declarations, logical key handling, measurement invocation, and display-model rendering. The package instance owns submenu selection and presentation state. MainApplication supplies the port service and invokes the package at serialized session boundaries.

This is preferred over keeping a generic host submenu with device-specific callback switches because it removes the duplicate device catalog and lets each package evolve its actions and presentation with its protocol. A separate UI package would add package pairing and lifecycle complexity without helping measurement devices.

### Return declarative bounded display models

Packages will return a fixed-size display model containing bounded title/text rows, selected action state, status information, and optional footer hints. MainApplication validates the model and renders it in the existing host-owned body region below the header. The package never receives a T6963 handle or drawing primitive.

This preserves the existing LCD ownership boundary while moving device-specific formatting out of MainApplication. Rejecting oversized models is preferred over truncation because truncated measurement values and status text can be misleading.

### Forward logical keys after host filtering

MainApplication will continue decoding raw keyboard bytes and handling global keys, back, and cancel. Only remaining logical events are forwarded to the active package. The package returns an explicit result such as consumed, unsupported, request-back, or error, allowing the host to retain session and navigation policy.

This avoids exposing keyboard protocol details and prevents packages from bypassing global safety/navigation controls. It also allows Fennel1 to reject directional actions based on its declared capability rather than relying on host-side conditionals.

### Split package sources while sharing only protocol/runtime helpers

The SDK will place Elta3 sources and metadata under `application/plugins/measurement-plugin/elta3` and Fennel1 sources and metadata under `application/plugins/measurement-plugin/fennel1`. Small protocol and ABI-independent runtime helpers may remain in a shared SDK area. Each package gets its own descriptor, identity, build target, tests, and `.pmp` output.

The technical wire-protocol name FTS3XX may remain in parser/helper identifiers where useful, but the installed package identity and user-facing metadata will be Fennel1. This separates product identity from the protocol implementation name.

### Version and validate the contract before activation

The descriptor and runtime API will advertise a UI/measurement contract version and structure sizes. The host validates these, required service UUIDs, bounds, interface kind, and package identity before creating an instance. Incompatible packages are rejected or represented by the existing diagnostic unavailable state and never receive keyboard or display callbacks.

This is a deliberate breaking ABI boundary. Silent fallback to host-side device behavior would recreate the dependency this change removes and could execute a package with incompatible memory layouts.

### Retain stable package identities

Elta3 and Fennel1 will retain their established package IDs unless implementation discovery proves that the IDs are reserved exclusively for the removed built-in path. Existing migration behavior that clears stale legacy assignments remains authoritative; a package is active only when its matching external package is present, valid, assigned, and compatible with the selected port.

## Risks / Trade-offs

- [ABI expansion can break existing packages] -> Version the contract, validate structure sizes before execution, and mark incompatible packages unavailable with tests for older descriptors.
- [A package can return malformed display data] -> Validate every bound and reject the complete model instead of truncating individual strings or rows.
- [Moving formatting into packages can duplicate common display code] -> Keep generic bounded model helpers in the SDK, but keep device-specific labels and result formatting in each package.
- [Host and package session lifetimes can become mismatched] -> Keep port service/context validity through package destruction and test teardown ordering explicitly.
- [Legacy code may remain reachable through shared measurement helpers] -> Remove production source-list and include dependencies, then use symbol/search checks and host build tests to verify MainApplication has no device-specific dispatch path.
- [Only one package may be installed] -> Treat package discovery independently and preserve valid package operation when the other package is absent or invalid.

## Migration Plan

1. Define and version the package-owned measurement UI/input/display contract and extend descriptor validation.
2. Adapt the SDK runtime and test harness to expose action declarations, logical-key dispatch, measurement state, and bounded display models.
3. Split Elta3 and Fennel1 sources, descriptors, metadata, build targets, and package output names.
4. Implement Elta3 directional actions and result presentation in the Elta3 package; implement Fennel1 measurement-only behavior and presentation in the Fennel1 package.
5. Change MainApplication to invoke the generic package session contract, render validated display models, and forward only host-approved logical keys.
6. Remove legacy `ef800_device_plugin` production dependencies and host-side device-specific submenu/formatting branches.
7. Add native package/host tests, package verification tests, discovery compatibility tests, and target build checks.
8. Validate independent `.pmp` installation, missing-package behavior, assignment/readiness behavior, session teardown, and all required firmware/image gates.

Rollback is package-compatible only at the firmware set level: revert the host and package contract together, or keep the new host in unavailable-package mode. Do not activate a new host against packages that lack the required contract version.

## Open Questions

- Confirm whether stable external package IDs `2` and `3` are retained for Elta3 and Fennel1 after legacy assignment cleanup.
- Confirm the exact display-model bounds and whether existing `ef800_plugin_screen` limits are sufficient for both packages.
- Confirm whether package output should be named `elta3.pmp` and `fennel1.pmp`, or retain a suffix such as `elta3_plugin.pmp` for deployment compatibility.
- Determine whether the PluginManager authoritative ABI needs changes in its upstream module or whether the EF800 extension can remain application-owned.
