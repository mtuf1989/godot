---
name: godot-shader-spatial
description: |
  Create or update bounded Godot 4 `spatial` shader effects for 3D material surfaces and Compositor-based post-processing using production-safe authoring, Forward+ awareness, and honest validation.
  Use when a real project needs water shaders, terrain splatting, character materials (skin SSS, hair anisotropy, eye parallax), foliage translucency and wind, glass refraction, hologram or sci-fi surfaces, or any non-VFX 3D spatial shader material.
  Also use when someone asks about Compositor Effects architecture, custom bloom, depth of field, motion blur, global outlines, or other viewport-level post-processing built as CompositorEffect.
  Also use when the task involves parallax occlusion mapping, triplanar mapping, stencil buffer effects (outline, X-ray), custom DEPTH writes, multi-pass rendering (Material Overlay, Next Pass), render priority, transparency sorting, or Forward+ vs Mobile renderer constraints for spatial shaders.
  Also use when someone mentions `shader_type spatial`, `ShaderMaterial` on MeshInstance3D, `StandardMaterial3D` conversion to shader code, `hint_screen_texture`, `hint_depth_texture`, `hint_normal_roughness_texture`, `CompositorEffect`, `RenderSceneBuffersRD`, `RenderSceneData`, `SSS_STRENGTH`, `ANISOTROPY`, `ANISOTROPY_FLOW`, `BACKLIGHT`, `DEPTH` output, `render_priority`, `Material Overlay`, `Next Pass`, or asks to "make a water shader," "add terrain blending," "create a skin material," "add post-processing," "implement bloom," "add depth of field," or "make a glass material" in a Godot project.
  Do NOT use for VFX-motivated spatial shaders (energy shields, distortion planes, particle mesh shaders) or VFX-motivated screen-space effects — those belong to godot-vfx.
  Do NOT use for 2D canvas_item shader effects — those belong to godot-shader-canvasitem-fx.
  Do NOT use for raw RenderingDevice compute shaders (boids, SPH, spatial hashing) — those belong to godot-compute.
  If the task is to build, fix, or integrate a 3D spatial shader material or a Compositor post-processing effect in Godot, this skill should fire.
---

# Godot Shader Spatial

Build bounded 3D `spatial` shader materials and Compositor-based post-processing effects that survive real project integration, Forward+ renderer constraints, and transparency pipeline pressure.

Read `references/material-catalog.md` first for material tasks. Read `references/compositor-effects.md` first for post-processing tasks. Read `references/advanced-techniques.md` when the task involves POM, triplanar, stencil, or multi-pass. Read `references/integration-checklist.md` before wiring the effect into scenes or scripts.

## Responsibility

- Inspect the real project surfaces the shader touches before editing:
  - target scenes and the exact `MeshInstance3D`, `WorldEnvironment`, or `Camera3D` nodes receiving the shader or compositor
  - current `.gdshader`, `.tres`, `.material`, `.gd`, and `.tscn` integration points nearby
  - project rendering assumptions: Forward+ vs Mobile, HDR/tonemap settings, TAA, MSAA, SDFGI/SSR/SSAO
  - the existing animation, scripting, or gameplay path that should drive the effect
  - whether the task is a material (surface shader) or a compositor effect (viewport post-processing)
- Create or update bounded `spatial` shader work for:
  - environment materials: water, terrain, foliage
  - character materials: skin, hair, eyes
  - technical materials: glass, hologram, sci-fi surfaces
  - compositor post-processing: bloom, DOF, motion blur, outlines, tonemap, screen-space effects
  - advanced techniques: POM, triplanar, stencil, custom depth, multi-pass
- Keep the shader production-safe:
  - snake_case names
  - grouped inspector parameters with `group_uniforms`
  - explicit type hints: `source_color`, `hint_default_white`, `hint_normal`, `hint_screen_texture`, `hint_depth_texture`, `filter_*`, `repeat_enable`
  - `instance uniform` for per-entity runtime state where batching matters
  - branch-light math using `mix()`, `step()`, `smoothstep()`, and `clamp()` where possible
  - explicit render mode declarations: `blend_*`, `depth_draw_*`, `cull_*`, `depth_test_*`
- Use direct file edits for `.gdshader`, `.tres`, `.glsl`, and supporting `.gd` files. When editor-connected, use MCP scene tools for editor-owned hierarchy changes or material assignment on existing scenes.
- Validate with the strongest honest method available and report renderer or pipeline limits without bluffing.

## Use When

- The project needs a concrete 3D spatial shader material for environment, character, or technical surfaces.
- The project needs a Compositor-based post-processing effect (bloom, DOF, motion blur, outlines, tonemap).
- A task depends on `ShaderMaterial` integration on `MeshInstance3D`, `StandardMaterial3D` conversion to shader code, or per-instance spatial shader parameters.
- Advanced spatial techniques are needed: POM, triplanar, stencil, custom DEPTH, multi-pass, render priority.
- Forward+ vs Mobile renderer constraints need to be navigated for a spatial shader or compositor effect.

## Do Not Use When

- The task is VFX-motivated spatial shaders (energy shields, distortion planes, particle mesh shaders) or VFX screen-space effects — route to `godot-vfx`.
- The task is 2D `canvas_item` shader effects — route to `godot-shader-canvasitem-fx`.
- The task is raw RenderingDevice compute shaders for inter-particle communication or general-purpose GPU computation — route to `godot-compute`.
- The task is WorldEnvironment/Environment configuration (tonemap preset, SSAO, SSR, SDFGI settings) without a custom shader or compositor need — route to `godot-architect`.
- The task is game feel/juice feedback (screen shake, hit pause, scale pops) — route to `godot-feel`.
- Architecture, resource ownership, or the decision to use a shader at all is still unresolved — route to `godot-architect` or `godot-scope`.

## Task Router

Before implementation, classify the task into one of two domains. This determines which primary reference to read.

### Material Tasks
Surface shaders applied to `MeshInstance3D` nodes. Read `references/material-catalog.md` first.
- Water, terrain, foliage, skin, hair, eyes, glass, hologram, sci-fi
- POM, triplanar, stencil, custom depth, multi-pass (also read `references/advanced-techniques.md`)

### Compositor Tasks
Viewport-level post-processing via `CompositorEffect`. Read `references/compositor-effects.md` first.
- Custom bloom, DOF, motion blur, global outlines, tonemap, screen-space stylization
- Multi-pass compute or fragment pipelines using `RenderSceneBuffersRD`

## Workflow

1. Read the Operating Model and the appropriate primary reference, then inspect the real targets:
   - exact scene or scenes involved
   - the receiving node type: `MeshInstance3D` for materials, `WorldEnvironment` or `Camera3D` for compositor
   - current materials, shaders, and any existing compositor stack
   - the script, animation, or gameplay flow that should drive the effect
   - project rendering facts: renderer, HDR, TAA, MSAA, glow settings, transparency usage
   In editor-connected mode, use `find_nodes` with type `MeshInstance3D`, `WorldEnvironment`, or `Camera3D` for targeted lookups.

2. Confirm the effect stays inside the skill boundary. Pick the smallest pattern from the appropriate reference that satisfies the task and state any assumptions that were not discoverable.

3. For material tasks, ground material ownership before editing:
   - shared `ShaderMaterial` plus `instance uniform` is the default for per-entity runtime state
   - enable `Local to Scene` only when a standard uniform truly must diverge per scene instance
   - start from `StandardMaterial3D` / `ORMMaterial3D` when possible, convert to shader code only when custom behavior is needed
   - choose the correct render pipeline: opaque for terrain/walls, alpha-tested for foliage, transparent only when required (water, glass)
   - use `Material Overlay` for additive passes (hologram shells, shield overlays)
   - use `Next Pass` for stencil-driven second passes (outlines, X-ray)

4. For compositor tasks, ground the pipeline architecture:
   - persistent `Compositor` on `WorldEnvironment` for global effects; camera-level override for special cameras
   - `EFFECT_CALLBACK_TYPE_POST_TRANSPARENT` as the default stage for most post-processing
   - declare buffer needs up front: `access_resolved_color`, `access_resolved_depth`, `needs_motion_vectors`, `needs_normal_roughness`
   - use `RenderSceneBuffersRD` custom contexts for intermediate textures
   - ping-pong any pass that samples neighbors
   - file-backed `.glsl` shaders for precompilation, not runtime string assembly
   - disable conflicting built-in effects when custom replacements are active

5. When the task touches editor-owned scene structure, use MCP in this order:
   - `open_scene` for the target scene
   - `scene://current/tree` to inspect the hierarchy before mutation, or `find_nodes` to locate target nodes
   - `add_node` for required host nodes
   - `update_property` for material references, compositor assignments, render settings
   - `connect_signal` only when the task includes effect triggering through gameplay events

6. Implement the smallest coherent shader and glue change. Read `references/integration-checklist.md` for wiring rules.

7. In editor-connected mode, call `refresh_filesystem` after changing `.gdshader`, `.tres`, `.glsl`, `.gd`, or starter `.tscn` files. Call `save_scene` after MCP scene mutation.

8. Validate with the strongest honest method available:
   - `get_diagnostics` on changed scripts and shaders
   - `run_scene` to verify runtime behavior
   - `editor_screenshot` after `run_scene` to visually verify shader effects in the running game
   - material behavior: correct render pipeline (opaque/alpha-test/transparent), correct depth interaction, correct lighting response
   - compositor behavior: correct pipeline stage, correct buffer access, no double-processing with built-in effects
   - performance: overdraw on transparent materials, texture fetch count on triplanar/POM, pass count on compositor chains

9. Escalate instead of improvising:
    - broad scope or unresolved effect direction → `godot-scope`
    - renderer or architecture decisions are still open → `godot-architect`
    - shader/material ownership or scene instancing is risky → `godot-scene-resource`
    - VFX-motivated spatial shaders or particles → `godot-vfx`
    - 2D canvas_item shader effects → `godot-shader-canvasitem-fx`
    - raw compute shaders → `godot-compute`
    - ordinary gameplay glue after the shader choice is already made → `godot-gdscript`
    - reusable skill-process failure was discovered → `godot-retro`

## Output

Leave behind bounded shader work with:

- files, scenes, or resources created or changed
- project surfaces inspected before editing
- task domain identified (material or compositor) and the chosen pattern
- material ownership decisions or compositor pipeline architecture
- render pipeline choices (opaque/alpha-test/transparent, render mode flags)
- animation or script-routing choices
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The exact target nodes, materials, shaders, and nearby integration points were inspected before editing.
- The effect stays inside `spatial` shader or `CompositorEffect` scope and does not drift into VFX, 2D, or compute territory.
- The correct render pipeline was chosen: opaque for solid surfaces, alpha-tested for cutout foliage, transparent only when genuinely needed.
- Per-entity runtime parameters use `instance uniform` unless there is a grounded reason not to.
- Uniform hints, grouping, render modes, and depth behavior are explicit.
- Compositor effects declare buffer needs up front and use file-backed shaders for precompilation.
- Compositor effects do not conflict with active built-in post-processing (e.g., custom bloom + built-in glow both active).
- Forward+ vs Mobile constraints are acknowledged when the project targets both.
- Validation claims match the real operating mode.

## Failure Modes

- Do not turn this skill into a generic shader cookbook or rendering tutorial.
- Do not use transparent pipeline when opaque or alpha-tested would work.
- Do not ignore the Forward+-only nature of `needs_normal_roughness` when targeting Mobile.
- Do not stack heavy compositor effects without considering their combined pass cost.
- Do not use runtime string assembly for compositor shaders when file-backed `.glsl` enables precompilation.
- Do not claim visual, runtime, or profiler validation when only filesystem inspection was performed.
- Do not duplicate materials per entity when `instance uniform` would preserve batching.
- Do not write custom `DEPTH` without acknowledging the early-Z/Hi-Z cost.
- Do not assume POM self-shadowing is free — it doubles ray-march work per light.
- Do not ignore transparency sorting problems on water, glass, or hair materials.

## References

Read only as needed:

- `references/material-catalog.md` — material authoring patterns for water, terrain, foliage, skin, hair, eyes, glass, hologram
- `references/compositor-effects.md` — compositor post-processing patterns for bloom, DOF, motion blur, outlines, tonemap
- `references/advanced-techniques.md` — POM, triplanar, stencil, custom depth, multi-pass, render priority
- `references/integration-checklist.md` — integration and validation checklist for spatial shaders and compositor effects
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
