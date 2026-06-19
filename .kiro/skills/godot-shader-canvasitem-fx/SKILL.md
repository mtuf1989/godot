---
name: godot-shader-canvasitem-fx
description: |
  Create or update bounded Godot 4 `canvas_item` shader effects for gameplay sprites and UI surfaces using production-safe material ownership, atlas-aware UV handling, and honest validation.
  Use when a real project needs hit flash, dissolve, outline or glow, palette swap, gradient background, full-screen transition, or simple UV distortion on `Sprite2D`, `AnimatedSprite2D`, `TextureRect`, `ColorRect`, `Polygon2D`, `CanvasGroup`, or related `CanvasItem` nodes.
  Also use when someone asks about `canvas_item` shaders, `ShaderMaterial` setup for 2D nodes, `instance uniform`, `group_uniforms`, `AtlasTexture` or `REGION_RECT` issues, palette LUT filtering, outline clipping, premultiplied alpha, gradient banding or dithering, `SCREEN_PIXEL_SIZE` aspect correction, or why a 2D shader breaks batching or stretches on atlases.
  If the task is to build, fix, or integrate a reusable 2D CanvasItem shader effect in Godot, this skill should fire.
---

# Godot Shader CanvasItem FX

Build narrowly scoped 2D `canvas_item` shader effects that survive real project integration, renderer constraints, and batching pressure.

Read `references/pattern-catalog.md` first. Read `references/integration-checklist.md` before wiring the effect into scenes or scripts. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the real project surfaces the effect touches before editing:
  - target scenes and the exact `CanvasItem` nodes receiving the shader
  - current `.gdshader`, `.tres`, `.material`, `.gd`, and `.tscn` integration points nearby
  - whether the texture source is standalone, region-enabled, or atlas-backed
  - project rendering assumptions such as pixel-art filtering, HDR usage, and the target 2D renderer
  - the existing animation or scripting path that should drive the effect
- Create or update bounded `canvas_item` shader work for:
  - hit flash
  - dissolve or materialize
  - outline or glow
  - palette swap
  - gradient backgrounds or panels
  - full-screen screen transitions on UI overlays
  - simple UV distortion or warp
- Keep the shader production-safe:
  - snake_case names
  - grouped inspector parameters with `group_uniforms`
  - explicit type hints such as `source_color`, `hint_default_white`, `filter_nearest`, and `repeat_enable`
  - `instance uniform` for per-entity runtime state
  - branch-light math using `mix()`, `step()`, `smoothstep()`, and `clamp()` where possible
- Use direct file edits for `.gdshader`, `.tres`, and supporting `.gd` files. When editor-connected, use MCP scene tools for editor-owned hierarchy changes or material assignment on existing scenes.
- Validate with the strongest honest method available and report renderer or atlas limits without bluffing.

## Use When

- The project needs a concrete 2D shader effect on gameplay sprites or UI surfaces.
- A task depends on `ShaderMaterial` integration, per-instance 2D shader parameters, or batch-safe runtime control.
- Atlas-backed sprites, palette swaps, transitions, or full-screen UI effects are breaking and need bounded fixes.
- The requested effect fits one of the catalog patterns without becoming a general rendering-system redesign.

## Do Not Use When

- The task is 2D particle effects, multi-node VFX compositions, trail rendering, or screen-space post-processing — route to `godot-vfx-2d`.
- The task is mainly about 3D `spatial` shaders, particles, volumetrics, compute work, or broad post-processing architecture.
- The main problem is UI layout, menu structure, or scene flow rather than the shader effect itself.
- The request is broad shader education or renderer theory rather than bounded project work.
- Architecture, resource ownership, or the decision to use a shader at all is still unresolved.
- The task is primarily about character/entity animation (AnimationPlayer setup, AnimationTree state machines, sprite animation, procedural animation) rather than shader effects — route to `godot-animation`.
- The task is really about source art creation, padding, or atlas packing rather than shader integration.

## Workflow

1. Read `references/pattern-catalog.md` for pattern selection, then inspect the real targets (read `references/integration-checklist.md` when wiring begins):
   - exact scene or scenes involved
   - the receiving node type and whether one node or many instances share the material
   - current textures, atlas or region usage, and any existing shader or material files
   - the script, animation, or UI flow that should drive the effect
   - project rendering facts that matter, especially filtering, HDR glow expectations, and likely batching sensitivity
2. Confirm the effect stays inside the skill boundary. Pick the smallest pattern from `references/pattern-catalog.md` that satisfies the task and state any assumptions that were not discoverable.
3. Ground material ownership before editing:
   - shared `ShaderMaterial` plus `instance uniform` is the default for per-entity runtime state
   - enable `Local to Scene` only when a standard uniform truly must diverge per scene instance
   - use a dedicated `CanvasLayer` plus full-rect `ColorRect` for screen transitions
   - if one logical entity spans multiple sprites, evaluate whether a parent `CanvasGroup` host is the right integration point
4. When the task touches editor-owned scene structure, use MCP in this order:
   - `open_scene` for the target scene
   - `scene://current/tree` to inspect the hierarchy before mutation, or `find_nodes` to locate target `CanvasItem` nodes by type or group
   - `add_node` for a `CanvasLayer`, `ColorRect`, or other required host nodes
   - `update_property` for material references, layer ordering, `texture_filter`, scripts, or other node properties
   - `connect_signal` only when the task includes effect triggering through buttons, state changes, or scene flow
5. Implement the smallest coherent shader and glue change:
   - create or update the `.gdshader` with `shader_type canvas_item;`
   - keep uniforms grouped and type-hinted for inspector safety
   - preserve alpha intentionally, including premultiplied-alpha cases when the project uses them
   - prefer branch-light math over `if` chains when the same result can be achieved with interpolation or thresholds
   - treat atlas or region-backed textures as a special case and do not trust raw generated UV assumptions without verifying local-region handling
   - enforce palette LUT nearest filtering and scrolling-noise repeat settings when those patterns apply
6. Route animation through the least fragile mechanism:
   - use `AnimationPlayer` for authored one-shots, loops, and deterministic effect timing
   - use `create_tween()` for lightweight runtime parameter changes such as hit flashes or hover gradients
   - prefer built-in `TIME` for continuous warp motion instead of pushing uniform updates every frame unless the effect truly needs CPU-driven timing
7. Call `refresh_filesystem` after changing `.gdshader`, `.tres`, `.gd`, or starter `.tscn` files that the editor must reload. If scene mutation happened through MCP, call `save_scene` after the structure and property changes are complete.
8. Read `references/examples-and-validation.md`, then validate with the strongest honest method available:
   - `editor_screenshot` after `run_scene` to visually verify shader effects in the running game — this is particularly valuable for shader work where visual correctness is the primary validation
   - one-instance versus many-instance material behavior
   - atlas safety and local-region behavior
   - outline padding, alpha integrity, and filtering correctness
   - full-screen transition layering, aspect ratio behavior, and overdraw risks
   - likely batching stability or pipeline pre-warm needs for the renderer in use
9. Escalate instead of improvising:
   - broad scope or unresolved effect direction -> `godot-scope`
   - renderer or architecture decisions are still open -> `godot-architect`
   - shader/material ownership or scene instancing is risky -> `godot-scene-resource`
   - the real fix is source-asset padding, placeholder textures, or atlas prep -> `godot-prototype-assets-2d`
   - the task is mainly ordinary gameplay glue after the shader choice is already made -> `godot-gdscript`
   - reusable skill-process failure was discovered -> `godot-retro`

## Output

Leave behind bounded shader work with:

- files, scenes, or resources created or changed
- project surfaces inspected before editing
- the chosen pattern and why it fits
- material ownership decisions and whether `instance uniform` or `Local to Scene` was used
- animation or script-routing choices
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The exact target nodes, materials, textures, and nearby integration points were inspected before editing.
- The effect stays inside `canvas_item` scope and does not drift into broad rendering architecture.
- Per-entity runtime parameters use `instance uniform` unless there is a grounded reason not to.
- Uniform hints, grouping, alpha behavior, and filtering rules are explicit.
- Atlas or region-backed textures are treated as a first-class integration risk when generated UV logic is involved.
- Full-screen transitions are isolated to dedicated overlay nodes and do not silently compete with gameplay draw order.
- Animation and runtime driving avoid unnecessary `_process`-based uniform churn when `TIME`, `AnimationPlayer`, or `Tween` can do the job more safely.
- Validation claims match the real operating mode.

## Failure Modes

- Do not turn this skill into a generic shader cookbook or rendering tutorial.
- Do not duplicate materials per entity when `instance uniform` would preserve batching.
- Do not assume raw `UV` math stays correct on `AtlasTexture` or region-backed sprites.
- Do not leave palette LUT filtering at linear when exact palette rows matter.
- Do not ignore transparent padding requirements for outline or burn-edge effects.
- Do not stack heavy full-screen effects casually and then claim performance safety.
- Do not drive simple one-shot shader animation with manual `_process` updates if a tween or animation track would be clearer.
- Do not claim visual, runtime, or profiler validation when only filesystem inspection was performed.

## References

Read only as needed:

- `references/pattern-catalog.md`
- `references/integration-checklist.md`
- `references/examples-and-validation.md`
- `../../foundation/Godot Nuanced Development Practices.md`
