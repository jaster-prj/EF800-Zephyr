# Plugin-Owned Measurement Navigation and Assignment

- Status: accepted
- Date: 2026-10-09
- Supersedes: None

## Context

ADR-0001 establishes that measurement packages own logical submenu declarations,
logical input handling, and measurement presentation while EF800Application owns
hardware and resource boundaries. The current implementation still exposes
measurement devices to users primarily as ports and contains plugin-specific
IDs, names, and serial-default branches in EF800Application. This prevents the
package-owned UI boundary from being reflected in configuration and global
measurement navigation.

## Decision

Measurement plugins will be the user-facing identity for measurement
assignment and navigation. Versioned plugin metadata will declare the plugin
identity, required hardware, serial defaults, readiness-compatible action
capabilities, and submenu/action model needed by the host. EF800Application will
use generic registry and host APIs to show compatible plugins during port
assignment, configure the selected port, build the Measurement menu from ready
assigned plugins, resolve a selected plugin to its internal port transport, and
render/route the plugin-owned submenu.

Port IDs remain internal resource-assignment details and are not the primary
identity of Measurement-menu entries. EF800Application will not contain
instrument-specific plugin IDs, names, protocol defaults, or action branches.

## Consequences

- Plugins can be added or renamed without changing EF800Application navigation
  code.
- Plugin packages and metadata require explicit version and compatibility
  validation, including serial defaults.
- Assignment state must maintain a stable plugin-to-port mapping and enforce
  port capability and ownership constraints.
- Existing packages must be rebuilt or rejected as incompatible when the
  metadata contract changes.
- Workspace persistence remains host-owned and consumes generic measurement
  results, preserving the hardware and storage boundary from ADR-0001.
