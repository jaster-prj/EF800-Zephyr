# ADR Review Manifest

## ADR Review Completed

- Date: 2026-10-06
- Reviewer: OpenCode
- Change: isolate-plugin-faults

## In-Force ADR Context Reviewed

- `adr/0001-package-owned-measurement-ui.md` - Preserves package-owned logical UI and input while EF800 Application retains hardware, resource, and navigation ownership.

## Repository-Level ADRs Created

- `adr/0002-isolate-external-plugin-execution.md` - Requires external packages to run in a supervised unprivileged MPU domain with mediated services and conservative fault recovery.

## Notes

ADR-0002 does not supersede ADR-0001. It provides the execution and protection mechanism for ADR-0001's existing host/package ownership boundary.
