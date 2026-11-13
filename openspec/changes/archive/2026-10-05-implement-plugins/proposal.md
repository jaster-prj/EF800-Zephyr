## Why

The Elta3 and Fennel1 measurement packages currently contain measurement protocol code but leave keyboard actions, submenu state, and measurement display behavior in EF800 host code. This keeps device-specific behavior in EF800Application and prevents each `.pmp` package from being a complete, independently usable measurement plugin.

## What Changes

- Move Elta3 and Fennel1 implementations into separate package-oriented directories under `application/plugins/measurement-plugin/elta3` and `application/plugins/measurement-plugin/fennel1`.
- Extend the EF800 measurement plugin contract so a package can declare submenu entries, handle logical keyboard actions, and return bounded display content.
- Implement Elta3 submenu actions, directional commands, measurement state, and display model generation inside the Elta3 package.
- Implement Fennel1 measurement submenu behavior and display model generation inside the Fennel1 package.
- Keep raw keyboard framing, LCD hardware access, global system keys, port ownership, and plugin lifecycle in EF800Application.
- Remove device-specific submenu, keyboard-action, display-formatting, and legacy measurement dispatch dependencies from EF800Application.
- Build and validate Elta3 and Fennel1 as independently installable `.pmp` packages.
- **BREAKING**: Plugin package ABI and host/plugin service contracts gain package-owned UI and logical-input behavior; old measurement packages without the required contract version are rejected or treated as unavailable.

## Capabilities

### New Capabilities

- `measurement-plugin-owned-ui`: Measurement plugins own their logical submenu, keyboard actions, measurement presentation state, and bounded display model while EF800Application remains the host renderer and resource owner.

### Modified Capabilities

- None. No durable capability specifications exist under `openspec/specs/`; the existing plugin-integration and legacy-removal changes are historical planning artifacts rather than main specifications.

## Impact

- Plugin ABI and EF800 public contract headers used by PluginManager packages and EF800Application.
- `application/plugins/measurement-plugin` build layout, package descriptors, protocol/runtime sources, host tests, and `.pmp` output naming.
- `application/ef800-application` plugin host integration, measurement menu/session handling, display model rendering, and build source lists.
- Shared measurement and plugin registry code that still exposes legacy device-specific adapters.
- Native plugin, measurement, UI, package-validation, and firmware build verification.
- Installed package compatibility and persistence behavior for Elta3 and Fennel1 plugin IDs.
