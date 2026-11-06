# EF800 Zephyr Firmware

EF800 is a measurement instrument firmware migration from Mbed OS to Zephyr.
This documentation describes the STM32L496 firmware, the separate STM32L431
keyboard controller, and the behavioral Virtual EF800 platform. It reflects the
implementation baseline of 2026-10-30 and distinguishes software evidence from
hardware qualification.

## Documentation

### For operators
- [Operations](./13-Operations/operations.md)

### For developers
- [Local development](./14-Developer_Guide/local-development.md)

### Architecture
- [Introduction and goals](./01-Introduction_and_Goals/introduction.md)
- [Constraints](./02-Constraints/constraints.md)
- [Context and scope](./03-Context_and_Scope/context.md)
- [Solution strategy](./04-Solution_Strategy/solutions.md)
- [Building block view](./05-Building_Block_View/blocks.md)
- [Runtime view](./06-Runtime_View/runtime.md)
- [Deployment view](./07-Deployment_View/deployment.md)
- [Crosscutting concepts](./08-Crosscutting_Concepts/crosscutting.md)
- [Architectural decisions](./09-Architectural_Decisions/index.md)
- [Quality requirements](./10-Quality_Requirements/quality.md)
- [Risks and technical debt](./11-Risks_and_Technical_Debt/risks.md)
- [Glossary](./12-Glossary/glossary.md)

## Sources of truth

Subsystem details and hardware gates remain in [`../docs/`](../docs/); accepted
decisions remain in [`../adr/`](../adr/). Keep those records and the relevant
arc42 section synchronized when architecture changes. Test evidence is described
in [`../docs/testing.md`](../docs/testing.md), and QSPI/hardware gates are
described in [`../docs/qspi-migration.md`](../docs/qspi-migration.md) and
[`../docs/flash-layout.md`](../docs/flash-layout.md).

Diagrams are maintained as PlantUML source under
[`diagram_as_code/`](diagram_as_code/README.md). Rendered SVGs are committed under
`diagram_as_code/out/` when PlantUML tooling is available. Section pages link to
the source and describe the view so the architecture remains readable without
generated assets.
