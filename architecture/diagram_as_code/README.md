# Diagram as Code

PlantUML sources are organized by arc42 view:

```text
diagram_as_code/
  05_building_block_view/  context and component blocks
  06_runtime_view/         runtime sequence diagrams
  07_deployment_view/      physical and virtual deployment
  out/                     optional rendered SVG exports
```

PlantUML source is versioned as text. Generated SVGs are optional documentation
assets, not firmware artifacts. Use PlantUML CLI to render sources while
preserving the matching subdirectory under `out/`, for example:

```sh
plantuml -tsvg -o ../out/05_building_block_view 05_building_block_view/*.puml
```

If PlantUML is not installed, the section files still describe each view and link
to its editable source. Do not hand-edit a generated SVG; update the `.puml` and
render it again.
