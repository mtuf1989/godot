# Integration Checklist

Use this file before wiring a `canvas_item` shader into scenes, scripts, or UI flows.

## 1. Ground The Target

- Inspect the exact scene and receiving node before choosing the shader pattern.
- Confirm whether the target is:
  - a standalone texture
  - a region-enabled sprite
  - an `AtlasTexture`
  - a full-screen UI overlay
- Inspect the current material ownership:
  - one shared `ShaderMaterial`
  - `Local to Scene`
  - one-off duplicated resources
- Inspect the project settings or conventions that affect the effect:
  - renderer choice if known
  - pixel-art versus filtered 2D look
  - HDR glow expectations
  - existing animation or script paths

## 2. Choose Ownership Deliberately

- Default to one shared `ShaderMaterial` plus `instance uniform` for per-entity runtime state.
- Use `Local to Scene` only when a standard uniform must differ per scene instance and there is no safe shared-material path.
- If several sprites form one logical entity, consider a parent `CanvasGroup` host instead of duplicating the material across each child.
- For screen transitions, isolate the effect on a dedicated `CanvasLayer` with a full-rect `ColorRect`.

## 3. Build The Shader Asset Safely

- Keep the shader bounded:
  - `shader_type canvas_item;`
  - no unrelated 3D or post-process logic
- Organize inspector parameters with `group_uniforms`.
- Use explicit hints:
  - `source_color` for colors
  - `hint_default_white` or `hint_default_black` for optional textures
  - `filter_nearest` for palette LUTs
  - `repeat_enable` for scrolling distortion noise
- Preserve alpha intentionally.
- Prefer branch-light math over long `if` trees when the same behavior can be expressed with interpolation or thresholds.

## 4. Handle Atlas And Region Risk Early

- Do not assume raw generated `UV` math is safe on atlas-backed sprites.
- If the effect depends on normalized local UV space, verify one of these paths explicitly:
  - local region reconstruction using `REGION_RECT`
  - region data passed from script as a uniform
  - a project decision to avoid atlas-backed targets for that shader
- Test atlas-backed sprites separately from standalone textures.
- Document any atlas limitation instead of hiding it.

## 5. Wire Scene And Script Integration

- Editor-connected scene changes:
  - `open_scene`
  - `scene://current/tree`
  - `add_node` when a new host such as `CanvasLayer` or `ColorRect` is needed
  - `update_property` for material references, layer, filter, and script assignments
  - `save_scene` after scene mutation
- Disk-backed asset changes:
  - create or edit `.gdshader`, `.tres`, `.gd`, or starter `.tscn` files directly
  - `refresh_filesystem` before trusting editor state on changed files

## 6. Pick The Right Driver

- Use `AnimationPlayer` when:
  - the effect is authored as part of a scene or sequence
  - timing should be deterministic
  - multiple properties should move together
- Use `create_tween()` when:
  - one-shot runtime responses such as hit flash or hover feedback are enough
  - the script already owns the gameplay event
- Use built-in `TIME` when:
  - the effect is continuous and procedural, such as UV warp scrolling
  - no gameplay authority needs to step it manually
- Avoid pushing simple shader parameters from `_process()` every frame unless that extra control is required and justified.

## 7. Audit Performance Before Claiming Safety

### Compatibility Renderer

- Assume batching matters.
- If per-entity changes are applied through standard uniforms on duplicated materials, expect draw call growth.
- Prefer `instance uniform` for dynamic values such as hit-flash intensity or palette row.

### Forward+ And Mobile Renderers

- Assume first-use pipeline compilation can stutter.
- If a new effect is important during gameplay, plan a pre-warm path:
  - render a hidden or off-screen dummy instance
  - do it during loading or a safe warm-up window

### Full-Screen Effects

- Keep full-screen shaders lightweight.
- Avoid stacking several heavy full-screen overlays at once.
- Treat gradient panels and transition masks as overdraw-sensitive work.

### Multi-Tap Effects

- Outline or glow shaders cost more than single-sample effects.
- Restrict them to small sprite bounds when possible.
- Ensure any enable or disable control actually bypasses the expensive path.

## 8. Enforce Texture And Filter Rules

- Palette LUTs must stay on nearest filtering.
- Scrolling distortion noise must have repeat enabled.
- Pixel-art hosts may also need their receiving `CanvasItem.texture_filter` or project defaults checked so the shader result matches the intended look.
- If the effect depends on transparent padding, verify the source asset actually provides it.

## 9. Exit Checklist

- Pattern choice matches the real task.
- Target nodes, textures, and materials were inspected first.
- Material ownership is explicit.
- Atlas or region handling is explicit.
- Filter and repeat requirements are explicit.
- Animation routing is explicit.
- Performance risks were at least reviewed honestly for the current renderer and effect type.
- Validation claims match the real operating mode.
