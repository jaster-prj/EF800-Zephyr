# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-06
- Reviewer: OpenCode
- Change: virtual-ef800

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - Keeps package logical UI portable while MainApplication owns hardware and navigation.
- `adr/0002-isolate-external-plugin-execution.md` - Requires supervised plugin execution and preserves STM32L496 MPU/fault target gates that virtual testing cannot replace.

## Repository-Level ADRs Created

- `adr/0003-use-native-sim-for-behavioral-ef800.md` - Establishes `native_sim` with virtual adapters as the product behavioral simulation architecture and defines its qualification boundary.

## Notes

ADR-0003 complements ADR-0001 and ADR-0002. It does not supersede their package ownership or target isolation requirements.
