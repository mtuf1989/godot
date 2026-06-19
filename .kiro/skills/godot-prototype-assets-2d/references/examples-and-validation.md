# Examples and Validation

Use this file before finalizing prototype asset work.

## Example Input

Task:

- add placeholder assets for a top-down prototype
- cover player, enemy, pickup, hazard, and three UI icons
- keep the art cheap and consistent
- do not start a polished art pass

## Good Output Shape

```md
- files changed:
  - res://assets/proto_2d/entities/player_capsule.svg
  - res://assets/proto_2d/entities/enemy_brute_square.svg
  - res://assets/proto_2d/pickups/pickup_health.svg
  - res://assets/proto_2d/hazards/hazard_spikes.svg
  - res://assets/proto_2d/ui/icons/icon_play.svg
- project surfaces inspected:
  - project.godot
  - scenes/player/Player.tscn
  - scenes/ui/Hud.tscn
- sync or MCP harness steps:
  - `refresh_filesystem` after editing the SVG files
  - if a validation harness scene was allowed, create the starter `.tscn` on disk first, then use `open_scene`, `scene://current/tree`, `add_node`, `update_property`, and `save_scene`
- conventions used:
  - 64 x 64 world sprites with 4-unit outlines
  - 32 x 32 pickups with 3-unit outlines
  - 24 x 24 icons with 2-unit outlines
  - centered pivots
  - safe SVG subset only
- validation performed:
  - `refresh_filesystem` completed for the edited SVGs
  - SVG structure, folder placement, and grayscale silhouette rules were checked
- limits:
  - in-editor readability still requires stronger validation if the assets were not inspected in a consuming scene or harness
```

## Validation Ladder

- Editor-connected:
  - call `refresh_filesystem` after editing the SVGs
  - inspect them in the consuming scene or a small asset harness
  - if a harness scene was created or changed, use `open_scene`, `scene://current/tree`, and `save_scene` instead of silently editing an existing validation scene on disk
  - verify centered pivots, no clipping, and readable silhouettes at expected sizes
- Filesystem-only:
  - verify SVG structure, naming, folder placement, and scene references
  - check that the recipes, stroke widths, and padding rules are consistent
  - end with the exact editor or MCP validation step still required
- Blocked:
  - stop if the consuming project artifacts or allowed target folders are unknown
  - stop if a validation harness scene is required but the task does not permit creating a starter `.tscn` before `open_scene`

## Checklist

- Actual consuming scenes, controls, or nodes were inspected before new conventions were chosen.
- Editor-connected work names the `refresh_filesystem` step and any harness-scene MCP actions explicitly.
- The set covers only the requested categories.
- Player, enemy, pickup, and hazard are distinguishable in grayscale.
- The blur test passes: all assets in the set look roughly equal in visual density when squinted at.
- Directional shapes use optical centering (1-2 unit shift in pointing direction on 24 x 24 grid).
- Icons remain legible at 24 px and still acceptable at 20 px.
- No asset uses SVG text, filters, masks, clip paths, or embedded rasters.
- `width`, `height`, and `viewBox` are aligned.
- Strokes do not clip at the texture bounds (shapes inset by at least half the stroke width from every edge).
- Repeated paint attributes are consolidated on the root `<svg>` where possible (see SVG Pitfalls in `svg-recipes.md`).
- The pivot convention is explicit.
- Validation claims match the real operating mode.

## Scenario Checks

### Player

- Direction cue remains visible at smaller scale.
- Player silhouette stays rounder or friendlier than enemy silhouettes.

### Enemy

- Threat shape still reads as hostile when recolored to gray.
- Spikes, corners, or brute mass do not collapse into noise at smaller scale.

### Pickup

- The interior cue reads without relying on color.
- The pickup stays readable at small HUD-like sizes.

### Hazard

- The shape stays sharp and warning-like when placed against gameplay tiles.
- Outlines do not clip when the asset sits near edges.

### UI Icons

- Icons remain distinct at 24 px.
- Stroke width stays consistent across the icon set.
- Contrast remains adequate against the intended button or panel background.
- Directional icons (arrows, play, chevrons) look visually centred, not mathematically centred. Apply the 1-2 unit optical shift from `palette-and-scale-rules.md`.
- Line centres sit on integer coordinates for even stroke widths (the default). See the pixel alignment table in `palette-and-scale-rules.md`.

## Common Failures

- Player and enemy differ only by color.
- The icon set mixes unrelated stroke widths or corner styles.
- Shapes touch the canvas edge and rasterized outlines clip.
- The asset sneaks in gradients or polish that the task did not ask for.
- The task claims import validation even though `refresh_filesystem` never ran after SVG edits.
- The final note claims import validation that never happened.

## Escalate

- Scope is turning into a broader visual direction problem -> `godot-scope`
- The task now depends on project-level design tradeoffs -> `godot-architect`
- The asset process itself needs reusable correction -> `godot-retro`
