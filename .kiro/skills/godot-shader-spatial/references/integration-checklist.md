# Integration Checklist

Use this file before wiring a spatial shader material or CompositorEffect into scenes, scripts, or gameplay flows.

## 1. Ground The Target

### For Material Tasks

- Inspect the exact scene and receiving `MeshInstance3D` before choosing the shader pattern.
- Confirm the surface type:
  - opaque environment (terrain, walls, floors)
  - alpha-tested vegetation (foliage, grass, fences)
  - transparent surface (water, glass, hologram)
  - character surface (skin, hair, eyes)
- Inspect the current material ownership:
  - `StandardMaterial3D` that may need conversion to shader code
  - existing `ShaderMaterial` with custom `.gdshader`
  - `Material Overlay` or `Next Pass` already in use
- Inspect project rendering settings:
  - renderer: Forward+ vs Mobile
  - HDR, tonemap mode, glow settings
  - TAA, MSAA, screen-space effects (SSAO, SSR, SDFGI)
  - existing `WorldEnvironment` and `Camera3D` configuration

### For Compositor Tasks

- Inspect the existing compositor stack:
  - `WorldEnvironment.compositor` — is there already a `Compositor` resource?
  - `Camera3D.compositor` — any camera-level overrides?
  - existing `CompositorEffect` entries and their pipeline stages
- Inspect built-in post-processing that may conflict:
  - `Environment.glow_enabled` (conflicts with custom bloom)
  - `CameraAttributes` DOF settings (conflicts with custom DOF)
  - `Environment.tonemap_mode` (conflicts with custom tonemap)
- Confirm the target renderer supports the needed buffers:
  - `needs_normal_roughness` → Forward+ only
  - `needs_motion_vectors` → Forward+ and Mobile

## 2. Choose Ownership Deliberately

### Material Ownership

- Default to one shared `ShaderMaterial` plus `instance uniform` for per-entity runtime state.
- Use `Local to Scene` only when a standard uniform must differ per scene instance and there is no safe shared-material path.
- Start from `StandardMaterial3D` when possible. Convert to shader code only when custom behavior is needed.
- For overlay effects (hologram shells, shield passes), use `Material Overlay` on the `GeometryInstance3D`.
- For stencil-driven second passes (outlines, X-ray), use `Next Pass` on the material.

### Compositor Ownership

- Assign the `Compositor` resource to `WorldEnvironment` for global effects.
- Use camera-level `Compositor` overrides only for exceptional cameras (scopes, CCTV, photo mode).
- Events feed data into persistent compositor effects — do not spawn ad hoc effects.

## 3. Build The Shader Asset Safely

### Spatial Shader Rules

- `shader_type spatial;`
- Explicit render modes: `blend_mix`, `depth_draw_opaque`, `cull_back` as defaults. Override only when the pattern requires it.
- Organize inspector parameters with `group_uniforms`.
- Use explicit hints:
  - `source_color` for colors
  - `hint_normal` for normal maps
  - `hint_default_white` / `hint_default_black` for optional textures
  - `hint_screen_texture`, `hint_depth_texture`, `hint_normal_roughness_texture` for screen reads
  - `filter_*` and `repeat_enable` as needed
- Do NOT use `source_color` on value textures (height, roughness, metallic, AO, thickness).
- Prefer branch-light math over long `if` trees.

### Compositor Shader Rules

- Use file-backed `.glsl` compute shaders, not runtime string assembly.
- Keep the pass graph fixed and predictable.
- Use `RGBA16F` for HDR intermediates, `R16F` for single-channel data (CoC, masks).
- Ping-pong any pass that samples neighbors — never read and write the same image.
- Validate or recreate working textures inside the callback path — buffer lifetime is only guaranteed during rendering.

## 4. Handle Render Pipeline Risk Early

### Transparency Hazards

- Writing `ALPHA` pushes the material into the transparent pipeline.
- Transparent materials: no shadows, no screen texture capture, no instancing in Forward+, sorting by node position only.
- Use opaque or alpha-tested whenever possible. Reserve transparency for water, glass, hologram.
- If only a small section needs transparency, split into separate surfaces.

### Alpha Mode Selection

1. **Opaque**: fastest. Terrain, walls, skin, eyes (eyeball).
2. **Alpha Scissor**: hard cutout. Foliage, fences, grates. Participates in depth pre-pass.
3. **Alpha Hash**: dithered soft coverage. Hair, soft vegetation edges.
4. **Depth Pre-Pass** (`depth_prepass_alpha`): better sorting but loses instancing.
5. **Alpha Blend**: only when genuinely needed. Water, glass, hologram.

### Forward+ vs Mobile

- SSS, normal-roughness buffer, TAA, separate specular → Forward+ only.
- Custom `light()` → disabled when vertex lighting is forced (mobile default).
- Design fallback paths for Mobile when the project targets both renderers.

## 5. Wire Scene And Script Integration

### Editor-Connected Scene Changes

- `open_scene` for the target scene
- `scene://current/tree` or `find_nodes` to inspect before mutation
- `update_property` for material references, compositor assignments, render settings
- `add_node` when new host nodes are needed
- `save_scene` after scene mutation
- `refresh_filesystem` after disk edits the editor must see

### Compositor Wiring

- Create the `Compositor` resource (`.tres`) with the effect entries
- Assign to `WorldEnvironment.compositor` or `Camera3D.compositor`
- Disable conflicting built-in effects on the same `Environment` or `CameraAttributes`

### Script Integration

- Use `AnimationPlayer` for authored material transitions
- Use `create_tween()` for one-shot runtime parameter changes
- Use built-in `TIME` for continuous procedural animation (UV scrolling, wave motion)
- For compositor parameter updates: push values from gameplay scripts into the `CompositorEffect` resource's exported properties

## 6. Audit Performance Before Claiming Safety

### Texture Fetch Budget

- Triplanar: 3× samples per texture lookup. Slope-gate to avoid paying this everywhere.
- POM: 8–32 texture fetches per pixel for the ray march, doubled with self-shadowing.
- Terrain splatting: N layers × samples per layer. Use Texture2DArray to keep this manageable.
- Screen reads: `hint_screen_texture`, `hint_depth_texture` are captured once per frame. Transparent objects not included.

### Transparency Overdraw

- Transparent materials are the main fill-rate risk.
- Water, glass, hologram: keep to bounded screen coverage.
- Do not stack multiple transparent layers casually.
- Use `render_priority` to enforce intended layering when overlap is unavoidable.

### Compositor Pass Budget

- Each compositor effect is a full-screen (or reduced-resolution) pass.
- Bloom: 2 × mip_count passes (down + up) + threshold + composite.
- DOF: CoC + separation + 2 blur passes + composite.
- Motion blur: tile max + neighbor max + reconstruction.
- Budget the total pass count against frame time.

### Shader Warm-Up

- First-use pipeline compilation can stutter.
- File-backed `.glsl` shaders enable precompilation.
- Feature-priming: render hidden instances during loading to precompile pipelines.
- Use Godot 4.4+ ubershaders and pipeline precompilation.
- Use Godot 4.5+ shader baker for export-time baking.

## 7. Validate Honestly

### Editor-Connected Validation

- `get_diagnostics` on changed `.gd` and `.gdshader` files
- `run_scene` to verify runtime behavior (compile-clean does not mean plays correctly)
- `editor_screenshot` after `run_scene` to visually verify shader effects in the running game
- Check: correct render pipeline? correct depth interaction? correct lighting response? correct compositor stage?

### Material-Specific Checks

- Opaque materials: do they participate in depth pre-pass, shadow casting, screen texture capture?
- Alpha-tested materials: is the cutoff threshold correct? Do edges look acceptable with current MSAA?
- Transparent materials: is sorting correct? Are there artifacts at depth discontinuities?
- POM materials: does the displaced UV feed all texture lookups? Is distance fade working?
- Triplanar materials: do textures align across mesh boundaries? Is slope-gating active?

### Compositor-Specific Checks

- Does the effect run at the correct pipeline stage?
- Are buffer access flags set correctly (`access_resolved_color`, `access_resolved_depth`, etc.)?
- Are conflicting built-in effects disabled?
- Does the effect handle viewport resize correctly (recreate working textures)?
- Does the effect work with XR/multiview if needed (per-view loop)?
- Does the effect behave correctly when the scene is idle (no new frames)?

### Filesystem-Only Validation

- Verify shader files, material ownership, render modes, and uniform hints in source only.
- Confirm the task describes the exact runtime validation step still required.

## 8. Exit Checklist

- Task domain identified (material or compositor).
- Pattern choice matches the real task.
- Target nodes, materials, and rendering settings were inspected first.
- Material ownership is explicit (shared + instance uniform, Local to Scene, overlay, next pass).
- Render pipeline is explicit (opaque, alpha-tested, transparent, compositor stage).
- Render modes are explicit (`blend_*`, `depth_draw_*`, `cull_*`).
- Uniform hints are correct (`source_color` on colors, NOT on value textures).
- Forward+ vs Mobile constraints are acknowledged.
- Conflicting built-in effects are disabled when custom replacements are active.
- Performance risks were reviewed (texture fetches, transparency overdraw, compositor passes).
- Validation claims match the real operating mode.

## 9. Escalate

- The request is turning into broader rendering architecture → `godot-architect`
- The task is VFX-motivated (particles, energy shields, distortion planes) → `godot-vfx`
- The task is 2D canvas_item shader effects → `godot-shader-canvasitem-fx`
- The task requires raw compute shaders → `godot-compute`
- The main work has become ordinary gameplay glue → `godot-gdscript`
- Scene/resource ownership is risky → `godot-scene-resource`
- The skill workflow itself needs correction → `godot-retro`
