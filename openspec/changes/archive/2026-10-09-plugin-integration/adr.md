# ADR: Use Startup-Scoped Host-Controlled Plugin Integration

## Status

Accepted for this change

## Context

EF800 must support user-installed Measurement and Calculation plugins while preserving the existing USB MSD storage ownership model and the host-owned LCD/keyboard experience. PluginManager currently supports one interface per descriptor, while EF800Application owns port assignment, UART lifetime, persistence, workspace policy, and a fixed menu state machine.

## Decision

Plugin discovery is startup-scoped and occurs only when EF800Application can access internal file storage. Built-in and external plugins are registered through one EF800-owned registry. Measurement availability is derived from a valid package plus a persisted compatible ready port binding; Calculation availability does not require a port. Plugin UI is an optional capability of the primary interface and uses bounded declarative screen content and logical keyboard events. EF800Application retains ownership of the header, global keys, navigation/session lifetime, resources, and workspace mutation flow.

Hardware requirements are EF800 contract metadata. The generic PluginManager SHALL NOT interpret EF800 hardware capabilities or port semantics.

## Rationale

The existing application restart when changing USB MSD to CDC or none already provides a safe synchronization boundary. Startup-only discovery avoids hot-replacement races and does not require changing MSD behavior. An optional UI capability avoids expanding PluginManager v1 into a multi-interface package model. A bounded screen model prevents plugins from corrupting the host header or directly controlling the T6963 hardware.

## Consequences

- New packages become available after the next EF800Application startup with reachable storage, not immediately after file copy.
- Missing external assignments remain visible diagnostically and are not silently remapped.
- Plugins have less rendering freedom than a raw graphics API, but the host can preserve stable layout, safety, and testability.
- Package security and production execution isolation remain separate follow-up gates.
- EF800 package metadata must be versioned independently from generic PluginManager semantics.
