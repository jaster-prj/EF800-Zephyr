# Package-Owned Measurement UI and Input

- Status: accepted
- Date: 2026-10-05
- Supersedes: None

## Context

EF800 measurement plugins currently implement instrument protocols while EF800Application owns device-specific submenu actions, keyboard handling, and display formatting. That split makes the host aware of individual instruments and prevents an installed `.pmp` package from being a complete measurement implementation. EF800Application must nevertheless retain raw keyboard framing, LCD hardware access, port ownership, and global navigation.

## Decision

Measurement packages will own their logical submenu declarations, logical input handling, measurement presentation state, and bounded display-model generation. EF800Application will expose only validated host services and logical key events, render the package's bounded declarative model, and retain all hardware, resource lifetime, raw protocol, and global-navigation ownership.

The package contract will be explicitly versioned and structure-size validated before activation. Device-specific behavior will be implemented in independently packaged Elta3 and FTS3XX `.pmp` artifacts rather than compiled into EF800Application.

## Consequences

- New measurement packages can define their actions and presentation without host-side device tables.
- EF800Application remains a generic resource-owning host and cannot directly format instrument-specific results.
- The package ABI must include stable bounds and compatibility validation for action and display models.
- Existing packages that do not support the required contract must be rejected or treated as unavailable.
- Raw keyboard and LCD adapters remain host responsibilities, so packages are portable but cannot directly draw or consume device protocols.
