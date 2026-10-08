# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-07
- Reviewer: OpenCode
- Change: deploy-virtual-ef800-web

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - Retains host ownership of validated logical display and keyboard boundaries.
- `adr/0003-use-native-sim-for-behavioral-ef800.md` - Keeps Virtual EF800 on `native_sim` with a bounded interactive and automated interface and no hardware-qualification claims.

## Repository-Level ADRs Created

- `adr/0004-use-web-gateway-for-virtual-ef800-front-panel.md` - Places browser presentation and exclusive controller arbitration in a separate web gateway while preserving the simulator protocol for headless clients.

## Notes

ADR-0004 complements and does not supersede ADR-0003. No accepted ADR was modified.
