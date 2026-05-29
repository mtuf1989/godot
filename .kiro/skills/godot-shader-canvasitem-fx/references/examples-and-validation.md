# Examples and Validation

Use this file before finalizing `canvas_item` shader work.

## Example Input

Task:

- add a hit flash and dissolve effect to an enemy sprite setup
- keep one shared material path where possible
- avoid atlas bleed on the animated sprite sheet
- document the exact validation level reached

## Good Output Shape

```md
- files changed:
  - res://shaders/enemy_hit_flash.gdshader
  - res://shaders/enemy_dissolve.gdshader
  - res://materials/enemy_hit_flash.tres
  - res://scenes/enemies/Enemy.tscn
  - res://scripts/enemies/enemy.gd
- project surfaces inspected:
  - project.godot
  - scenes/enemies/Enemy.tscn
  - scripts/enemies/enemy.gd
  - current sprite sheet or atlas usage
  - any existing shader or material resources on the enemy
- MCP or sync steps:
  - `open_scene` on `res://scenes/enemies/Enemy.tscn`
  - `scene://current/tree` read before scene mutation
  - `update_property` for the material assignment on the target `CanvasItem`
  - `refresh_filesystem` after `.gdshader`, `.tres`, or `.gd` edits
  - `save_scene` if the scene hierarchy or properties were changed through MCP
- pattern and ownership choices:
  - hit flash uses `instance uniform flash_modifier`
  - dissolve uses shared structural uniforms and instance-driven progress
  - atlas-backed target was treated as a local-region risk and tested separately
- animation routing:
  - `create_tween()` drives the hit flash one-shot
  - `AnimationPlayer` or tween drives dissolve progression
- validation performed:
  - alpha preservation was checked
  - one-versus-many instance behavior was checked
  - atlas bleed risk was reviewed
- limits:
  - runtime profiler or renderer-specific stutter checks still require stronger editor-connected validation if they were not run
```

## Validation Ladder

- Editor-connected:
  - inspect the target scene and node hierarchy before mutation
  - `refresh_filesystem` after changing `.gdshader`, `.tres`, `.gd`, or starter `.tscn` files
  - re-open or re-read the target scene state after changes
  - run the effect in context and confirm the visual behavior on the real node
  - when performance risk matters, use the Godot profiler to inspect 2D draw calls or observe obvious first-use stutter
- Filesystem-only:
  - verify shader files, material ownership, script hooks, filter rules, and atlas-risk handling in source only
  - confirm the task describes the exact runtime validation step still required
- Blocked:
  - stop if the destination node, texture source, or allowed output location is unknown
  - stop if the task requires a new overlay scene or validation harness but does not permit creating the starter `.tscn` first

## Checklist

- Actual target scenes, nodes, textures, and materials were inspected first.
- The effect stays inside `canvas_item` scope.
- `instance uniform` is used for dynamic per-entity values when batching or shared-material behavior matters.
- Palette LUT filtering and noise repeat rules are explicit when those patterns apply.
- Atlas or region handling is explicit when generated UV logic is involved.
- Alpha preservation was checked.
- Full-screen transitions, if present, live on a dedicated overlay layer.
- Validation claims match the real operating mode.

## Scenario Checks

### Enemy Hit Flash Batching

- Place many identical enemies that share the same material.
- Flash one enemy at a time through an `instance uniform`.
- Confirm only the chosen enemy flashes.
- Confirm transparent space around the sprite does not become a solid box.
- If runtime profiling is available, confirm 2D draw calls do not spike just because one enemy flashed.

### Dissolve Atlas Bleed

- Apply the dissolve to an animated atlas-backed sprite.
- Drive progression from visible to fully dissolved.
- Confirm the noise pattern stays local to the sprite and does not sample neighboring packed frames.
- Confirm the sprite fully disappears without residue.

### Outline Margin Check

- Test an outlined sprite with no transparent padding at the texture border.
- Inspect all edges for flat clipping.
- If clipping appears, record that the asset needs transparent margin or the effect needs a different host strategy.

### Menu Gradient Banding Audit

- Stretch a gradient `ColorRect` across a large viewport.
- Use two dark, similar colors that would normally reveal banding.
- Compare the output with and without the shader's anti-banding or dithering logic.
- Confirm the fix improves the blend without adding obvious patterned noise.

### Screen Transition Aspect Check

- Run the transition on wide, standard, and tall aspect ratios.
- Confirm the overlay stays above gameplay and UI.
- Confirm generated diamonds, circles, or wipes keep their intended proportions.

### UV Distortion Edge Check

- Test the warp on a standalone texture first.
- Then test any atlas-backed version separately.
- Confirm the motion loops cleanly and does not create obvious edge streaks.
- Record atlas limitations honestly if the effect is unsafe there.

## Common Failures

- A per-entity flash or palette change duplicates the whole material instead of using `instance uniform`.
- A dissolve or distortion shader assumes raw `UV` math is safe on atlas-backed sprites.
- A palette LUT uses linear filtering and bleeds between rows.
- An outline is shipped without transparent padding and clips at the bounds.
- A full-screen transition is attached to the wrong node and loses draw-order control.
- The task claims runtime or profiler validation even though only source inspection happened.

## Escalate

- The request is turning into broader rendering architecture or post-processing work -> `godot-architect`
- The real blocker is asset preparation or sprite padding -> `godot-prototype-assets-2d`
- The main work has become ordinary gameplay glue around an already chosen effect -> `godot-gdscript`
- The skill workflow itself needs correction -> `godot-retro`
