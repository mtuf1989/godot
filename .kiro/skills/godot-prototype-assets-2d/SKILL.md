---
name: godot-prototype-assets-2d
description: |
  Create cheap, readable, consistent placeholder 2D assets for Godot with SVG-first prototype conventions.
  Use when gameplay work needs player, enemy, pickup, hazard, projectile, or UI icon placeholders plus import-safe naming, sizing, and honest validation without drifting into polished art direction.
  Also use when someone says "I need placeholder art," "make me some prototype sprites," "temporary game art," "placeholder icons," or needs quick visual stand-ins to unblock gameplay iteration.
  If the project needs any kind of cheap 2D visual assets for prototyping — not final art — this skill should fire.
---

# Godot Prototype Assets 2D

Create prototype-only 2D assets that unblock gameplay work without inventing a real art pipeline.

Read `references/palette-and-scale-rules.md` first. Read `references/svg-recipes.md` before editing asset files. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the actual project surfaces that consume the assets before editing:
  - existing asset folders
  - scenes or controls that will use the textures
  - relevant `project.godot` texture, filtering, or pixel-snap expectations
- Create or update simple placeholder SVG assets for gameplay and basic UI icons.
- Keep assets cheap, readable, consistent, and easy to extend:
  - silhouette first
  - color second
  - stable sizing and pivots
  - import-safe SVG features only
- Use direct file editing for SVGs, then `refresh_filesystem` when editor import or preview state must pick them up.
- Optionally add a small validation scene or layout harness when the task explicitly allows it.

## Use When

- Gameplay work is blocked on missing 2D art.
- The project needs placeholder player, enemy, pickup, hazard, or button/icon assets.
- The team needs a repeatable naming, sizing, palette, and pivot convention for prototype art.
- The task is still bounded enough to stay in placeholder territory.

## Do Not Use When

- The request is really about polished illustration, branding, marketing art, or a production asset pipeline.
- The work depends on shader polish, animation polish, particles, or layered visual FX as the main output.
- The main problem is UI layout, scene structure, or architecture rather than asset creation.
- The project needs a broad art direction pass instead of a small prototype set.

## Workflow

1. Read `references/palette-and-scale-rules.md` and `references/svg-recipes.md`.
2. Inspect the real targets before making conventions concrete:
   - current asset root and naming scheme
   - scenes or controls that will consume the assets
   - whether the project is normal 2D, zoom-heavy, or pixel-perfect
   - any existing palette or placeholder set that must be preserved
3. Pick the smallest coherent asset set that satisfies the task. Prefer canonical sizes and the safe SVG subset from `references/svg-recipes.md`.
4. Create or update the assets with these defaults unless grounded project facts require otherwise:
   - transparent background
   - matching `width`, `height`, and `viewBox`
   - flat fill plus outline
   - no SVG text
   - no filters, masks, clip paths, or embedded rasters
5. Place files in the concrete project folder using predictable names. If the project has no established convention, default to `res://assets/proto_2d/`.
6. Call `refresh_filesystem` after changing SVGs or related scene files before claiming the import pipeline or editor preview has picked them up.
7. If the task explicitly allows a validation harness scene, create a starter `.tscn` on disk first when needed, then use `refresh_filesystem`, `open_scene`, `scene://current/tree`, `add_node`, `update_property`, `duplicate_node` (to clone harness nodes for testing multiple asset variants), and `save_scene` to assemble the harness rather than editing an existing validation scene blindly on disk.
8. Read `references/examples-and-validation.md`, then validate with the strongest honest method available:
   - distinguishable silhouettes in greyscale
   - centered pivot behavior unless the grounded project proves a different rule
   - no stroke clipping at texture bounds
   - import-safe structure and settings
9. Escalate instead of improvising:
   - broad scope or art-direction ambiguity -> `godot-scope`
   - unresolved technical tradeoffs that change project structure -> `godot-architect`
   - reusable process failure in the skill itself -> `godot-retro`

## Output

Leave behind bounded prototype asset work with:

- files created or changed
- sync or MCP harness steps used when editor-connected
- asset categories covered
- folder and naming convention used
- palette and scale assumptions used
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- Assets are distinguishable by shape before color.
- Editor-connected work uses `refresh_filesystem` before claiming import or preview validation on changed assets.
- SVGs stay inside the safe import subset and avoid unsupported text or effect-heavy features.
- Sizes, padding, stroke widths, and pivots are consistent across the set.
- Naming and placement are explicit.
- Validation claims match the real operating mode.

## Failure Modes

- Do not turn placeholder work into a polished visual style system.
- Do not introduce gradients, glow, shader polish, or animation unless the task explicitly requires it and the scope still stays prototype-only.
- Do not invent pixel-perfect rules without inspecting whether the project actually uses them.
- Do not claim import validation on freshly edited SVGs if `refresh_filesystem` was never requested.
- Do not claim Godot import or editor validation when only filesystem inspection was performed.
- Do not widen the task into UI layout, scene redesign, or a full asset pipeline.

## References

Read only as needed:

- `references/palette-and-scale-rules.md`
- `references/svg-recipes.md`
- `references/examples-and-validation.md`
