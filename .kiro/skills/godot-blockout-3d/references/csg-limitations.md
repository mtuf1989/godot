# CSG Limitations

Use this file before adding CSG nodes or before deciding whether to keep existing CSG in place.

## What CSG Is For

- Treat CSG as a prototype tool, not a production modeling pipeline.
- Use it when fast boolean edits are the real advantage:
  - openings
  - subtractive shells
  - quick volume iteration
- Prefer primitive meshes when the same result can be achieved without boolean work.

## Performance Boundaries

- CSG has a meaningful CPU cost compared with primitive meshes.
- Large or deeply nested CSG trees slow iteration and make editor work less predictable.
- Runtime movement or animation of CSG geometry is a hard warning sign. Replace it with baked or explicit geometry first.
- Split unrelated CSG work into separate trees. Do not make one giant combiner own the whole level.

## Reliability Boundaries

- Do not treat CSG output as final topology.
- Avoid piling many nested boolean operations into one area if you start seeing holes, cracks, or missing faces.
- Avoid coplanar or nearly coplanar boolean surfaces when the result becomes brittle.
- When a boolean setup becomes hard to reason about, simplify it or bake it before adding more detail.

## UV and Material Boundaries

- CSG is not the right place for deliberate UV layout or polished material workflows.
- Triplanar mapping is acceptable for rough prototypes only. It is not a substitute for authored assets.
- If the request now depends on texel density, trim sheets, or material polish, the blockout phase is over.

## Collision Boundaries

- `use_collision` on CSG creates static-style collision only.
- Hidden CSG can still keep collision active, so do not assume hiding the node disables gameplay impact.
- Baked CSG collision becomes concave collision, which is slow and unsuitable as the default for gameplay-critical contact.
- If character movement depends on detailed steps, bevels, trims, or other small shapes, replace CSG-derived collision with explicit primitive or simplified collision.

## Handoff Triggers

Stop extending CSG and switch to bake, replace, or handoff when any of these are true:

- editor rebuild time is now slowing normal iteration
- the tree is producing holes or brittle boolean output
- gameplay needs moving or animated geometry
- collision needs precise feel rather than broad static coverage
- the task now requires UVs, materials, or authored environment art

## Bake and Replace Notes

- Bake static geometry when the layout is stable enough that iteration speed matters more than boolean flexibility.
- Pair baked geometry with explicit collision review. Do not assume the baked result is automatically fit for gameplay.
- If scripting a bake, remember CSG mesh updates are deferred by one frame after property changes.

## Escalate

- Scope is no longer prototype-only -> `godot-scope`
- Environment implementation now needs broader architecture or production decisions -> `godot-architect`
- Scene or resource ownership becomes risky during bake or replacement -> `godot-scene-resource`
