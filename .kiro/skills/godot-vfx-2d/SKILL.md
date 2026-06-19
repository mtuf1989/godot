---
name: godot-vfx-2d
description: |
  Create, configure, and optimize 2D visual effects in Godot 4 using GPUParticles2D,
  CPUParticles2D, ParticleProcessMaterial, custom particle shaders, flipbook animation,
  trail rendering (Line2D, particle trails, custom _draw()), CanvasGroup compositing,
  SubViewport isolation, screen-space post-processing (shockwaves, speed lines, CRT,
  vignette, chromatic aberration), BackBufferCopy, and Tween-based procedural VFX.
  Covers pixel-art VFX (nearest filtering, integer scaling, stepped motion, crunchy aesthetics),
  HD 2D VFX (smooth particles, normal-mapped sprites, soft blending, Light2D interaction),
  and stylized/anime 2D VFX (impact frames, speed lines, hard-edged shapes, screen flashes).
  Use when the user wants to build 2D particle effects, explosions, magic spells, fire, smoke,
  sparks, slash effects, hit flashes, screen distortion, shockwaves, weather effects, dust,
  trails, speed lines, impact frames, CRT overlays, damage numbers, or any 2D visual effect.
  Also use when someone mentions GPUParticles2D, CPUParticles2D, ParticleProcessMaterial for 2D,
  2D particle shaders, CanvasGroup VFX, BackBufferCopy, hint_screen_texture for 2D effects,
  SubViewport compositing for VFX, Line2D trails, flipbook particles, CanvasItemMaterial
  particle animation, visibility_rect, 2D overdraw, 2D batch breaking, CanvasLayer post-processing,
  Tween-based VFX, procedural animation VFX, or asks to "add 2D particles," "make a 2D explosion,"
  "create a slash effect," "add screen shake distortion," "make speed lines," "add a hit flash,"
  "make this 2D effect look cool," or "add 2D VFX."
  Even if the user just says "I need sparks when the player attacks" or "add some particles
  when the enemy dies" or "make a magic attack effect" in a 2D Godot project, this skill applies.
  Do NOT use for 3D particle effects (GPUParticles3D) — those belong to godot-vfx.
  Do NOT use for 2D canvas_item shader effects on existing nodes (hit flash, dissolve, outline,
  palette swap) — those belong to godot-shader-canvasitem-fx.
---

# Godot VFX 2D

Build bounded 2D visual effects using Godot 4's particle systems, screen-space compositing, trail rendering, Tween-based procedural animation, and CanvasItem post-processing.

Read `references/particle-systems-2d.md` first. Read the appropriate reference for the task domain before implementation. Read `references/optimization-2d.md` before finalizing any effect.

## Responsibility

- Inspect the actual project surfaces the VFX will integrate with before editing:
  - target scenes and the nodes where effects attach or spawn
  - existing particle systems, materials, shaders, and VFX conventions in the project
  - the art direction: pixel-art, HD 2D, or stylized/anime
  - camera setup, CanvasLayer structure, and viewport settings that affect VFX rendering
  - the gameplay trigger: what event spawns the effect, what script or signal drives it
  - performance context: target platform, existing particle budgets, known bottlenecks
- Create or update bounded 2D VFX work covering:
  - GPUParticles2D configuration and ParticleProcessMaterial setup
  - custom particle process shaders when the built-in material is insufficient
  - flipbook animation via CanvasItemMaterial or custom CanvasItem shaders
  - trail rendering via Line2D, GPUParticles2D trails, or custom `_draw()` geometry
  - CanvasGroup compositing for multi-element VFX bundles
  - SubViewport isolation for resolution-independent or masked VFX buffers
  - screen-space post-processing via CanvasLayer + ColorRect (shockwaves, speed lines, CRT, vignette, chromatic aberration)
  - BackBufferCopy for local screen-reading effects
  - Tween-based procedural VFX as lightweight alternatives to particles
  - multi-node VFX composition choreography
- Keep effects production-safe:
  - enforce overdraw awareness on every transparent particle system (no depth buffer in 2D)
  - protect 2D batching: share materials, group blend modes, keep texture state consistent
  - set `visibility_rect` tightly on GPUParticles2D
  - budget particle counts against fill-rate, not just simulation cost
  - respect pixel-art constraints when the project uses nearest filtering and integer scaling
- Use direct file edits for `.gdshader`, `.tres`, `.gd`, and supporting files. When editor-connected, use MCP scene tools for scene assembly and node configuration.
- Validate with the strongest honest method available and report rendering or performance limits without bluffing.

## Use When

- The project needs a concrete 2D visual effect: particles, trails, screen-space techniques, or procedural animation VFX.
- A GPUParticles2D or CPUParticles2D system needs to be created, configured, optimized, or debugged.
- The task involves custom 2D particle shaders, flipbook animation, or CanvasItemMaterial particle setup.
- Trail rendering is needed: Line2D, GPUParticles2D trails, or custom `_draw()` trails.
- CanvasGroup compositing is needed to merge multiple VFX elements as one drawable.
- SubViewport isolation is needed for resolution-independent VFX, effect-only masks, or accumulation buffers.
- Screen-space post-processing is required: shockwaves, speed lines, CRT, vignette, chromatic aberration, screen flash.
- BackBufferCopy control is needed for overlapping screen-reading effects.
- Tween-based procedural animation is the right tool: short-lived scale pops, flashes, damage numbers, shader uniform pulses.
- Multi-node VFX composition needs choreography: combining particles + trails + screen effects as one authored effect.

## Do Not Use When

- The task is 3D particle effects (GPUParticles3D, volumetric fog, spatial VFX shaders) — route to `godot-vfx`.
- The task is 2D canvas_item shader effects on existing nodes (hit flash, dissolve, outline, palette swap) — route to `godot-shader-canvasitem-fx`.
- The task requires raw RenderingDevice compute shaders for inter-particle communication — route to `godot-compute`.
- Architecture or scope is still undecided — route to `godot-architect` or `godot-scope`.
- The task is character/entity animation (AnimationPlayer, AnimationTree, sprite animation, procedural creature animation, walk cycles, attack combos) — route to `godot-animation`. This skill handles VFX animation (particles, trails, Tween-based effects); `godot-animation` handles entity animation.

## Art Direction Router

Before building any effect, identify the project's art direction. This determines filtering, blend modes, resolution handling, and which techniques apply.

### Pixel-Art Pipeline
- `filter_nearest` on all VFX textures and nodes — no exceptions
- `viewport` stretch mode with `integer` stretch scale mode
- `snap_2d_transforms_to_pixel` and/or `snap_2d_vertices_to_pixel` enabled
- Lossless texture imports (no VRAM compression for low-res pixel art)
- Stepped particle motion: `fixed_fps` with `interpolate` disabled for crunchy movement
- Integer camera/emitter positions — disable camera smoothing or snap it
- Prefer Tween-based procedural VFX and custom `_draw()` trails for pixel-locked behavior
- Additive sprites for transient flashes instead of real Light2D nodes
- Read `references/optimization-2d.md` section on pixel-art constraints

### HD 2D / Smooth Pipeline
- Linear or linear-mipmap filtering on VFX textures
- Normal-mapped particle sprites via CanvasTexture with `normal_texture`
- Light2D / PointLight2D interaction with particles — use `light_mask` partitioning
- Soft alpha blending with gradient textures
- HDR 2D (`use_hdr_2d`) for smooth glows and subtle color ramps
- SubViewport at different resolution for soft glow fields or low-frequency distortion buffers
- Mipmaps on particle textures for distance rendering
- Read `references/particle-systems-2d.md` section on Light2D interaction

### Stylized / Anime Pipeline
- Hard-edged discrete shapes, impact frames, speed lines
- Screen-space staging via CanvasLayer + ColorRect post-processing
- Additive blending for energy effects, `render_mode unshaded` for pure energy
- Flipbook-dominant animation with procedural UV distortion overlay
- Tween-driven screen flashes, scale pops, and shader uniform pulses
- Dynamic color correction via LUT shaders during VFX events
- Read `references/screen-space-and-compositing.md` for the full screen-space pipeline

### Hybrid
- Many 2D projects mix styles. Background weather may be smooth while combat VFX are pixel-art or stylized. Identify per-effect, not per-project.

## Tool Selection Guide

Before reaching for particles, consider whether a simpler tool fits the effect.

| Effect Type | Recommended Tool | Why |
|---|---|---|
| Short property animation on existing nodes (flash, pop, fade) | **Tween** | Cheapest, most deterministic, no overdraw cost |
| Reusable authored timeline with complex timing | **AnimationPlayer** | Visual editing, non-trivial timing curves |
| Many independently moving elements with lifetime/randomness | **GPUParticles2D** | Purpose-built simulation, GPU-accelerated |
| Low-end fallback or GPU-bottlenecked scene | **CPUParticles2D** | Moves simulation off GPU |
| Premium authored trail with gradients and caps | **Line2D** | Best authoring workflow for few trails |
| Many concurrent streaks (sparks, embers) | **GPUParticles2D trails** | GPU-driven, scales with count |
| Pixel-locked thin lines or bespoke geometry | **Custom `_draw()`** | Exact pixel control |
| Merging multiple VFX elements as one drawable | **CanvasGroup** | Prevents overlap opacity artifacts |
| Resolution-independent or masked VFX buffer | **SubViewport** | Isolation boundary |
| Full-screen camera polish (CRT, vignette, speed lines) | **CanvasLayer + ColorRect** | Standard post-processing path |
| Local screen distortion (shockwave, heat haze) | **hint_screen_texture shader** | Screen-reading on effect geometry |

## Workflow

1. Read `references/particle-systems-2d.md` and the art-direction-appropriate reference, then inspect the project surfaces:
   - the scene and nodes where the effect will live
   - existing VFX systems, materials, shaders, and naming conventions
   - camera, CanvasLayer structure, and viewport settings
   - the gameplay event or script that triggers the effect
   - target platform and any known performance constraints
   In editor-connected mode, use `find_nodes` with type "GPUParticles2D" or "CanvasGroup" for targeted lookups.

2. Confirm the effect stays inside the skill boundary. If the task needs 3D particles, raw compute shaders, or material-level canvas_item effects on existing sprites, escalate.

3. Select the right tool for the effect (see Tool Selection Guide above). Then design the effect structure:
   - for particle effects: choose GPUParticles2D vs CPUParticles2D, ParticleProcessMaterial vs custom shader, flipbook vs procedural
   - for trails: choose Line2D vs particle trails vs custom `_draw()` based on count and art needs
   - for screen-space effects: choose local distortion vs full-screen post-pass vs CanvasGroup compositing
   - for lightweight VFX: choose Tween vs AnimationPlayer based on complexity
   - for multi-element effects: plan the composition — which elements, what wrapper (CanvasGroup?), what trigger mechanism

4. Use MCP for scene assembly:
   - `open_scene` for the target scene
   - `scene://current/tree` to inspect hierarchy before mutation
   - `add_node` for GPUParticles2D, CanvasGroup, CanvasLayer, ColorRect, SubViewport, and supporting nodes
   - `update_property` for particle material references, transforms, visibility_rect, z_index, light_mask
   - `connect_signal` when VFX triggering requires signal wiring

5. Implement the effect:
   - configure ParticleProcessMaterial or write custom particle shader
   - create CanvasItemMaterial for flipbook animation or write custom CanvasItem shader
   - set up trail rendering (Line2D configuration, particle trail sections, or custom `_draw()` geometry)
   - for screen-space VFX: create dedicated CanvasLayer + ColorRect with screen-reading shader
   - for local distortion: configure BackBufferCopy region if multiple screen-readers overlap
   - for grouped VFX: wrap elements in CanvasGroup, configure fit_margin and clear_margin
   - for isolated buffers: configure SubViewport with transparent_bg, disable_3d, appropriate resolution
   - for Tween VFX: write the property sequence with appropriate easing, handle lifecycle and cleanup

6. Read `references/optimization-2d.md`, then apply optimization:
   - audit overdraw: count overlapping transparent layers at peak density (no depth buffer in 2D — everything is painter's algorithm)
   - protect batching: share materials, group blend modes, keep texture state consistent, avoid Z-index fragmentation
   - set `visibility_rect` tightly on GPUParticles2D
   - reduce `amount` (not `amount_ratio`) when particle budget is exceeded
   - for pixel art: verify nearest filtering, integer scaling, and snap settings
   - for lit particles: partition light_mask to limit PointLight2D coverage on VFX layers

7. In editor-connected mode, call `refresh_filesystem` after changing `.gdshader`, `.tres`, `.gd`, or `.tscn` files. Call `save_scene` after MCP scene mutation.

8. Validate with the strongest honest method:
   - `get_diagnostics` on changed scripts and shaders
   - `run_scene` to verify runtime behavior
   - `editor_screenshot` after `run_scene` to visually verify the effect — critical for VFX where visual correctness is the primary validation
   - check: does the effect trigger at the right moment? do particles respect visibility_rect? is overdraw within budget? does the pixel-art pipeline stay crisp?

9. Escalate instead of improvising:
    - broad scope or VFX design direction undecided → `godot-scope`
    - rendering architecture or pipeline decisions open → `godot-architect`
    - scene/resource ownership of VFX nodes is risky → `godot-scene-resource`
    - inter-particle compute (boids, SPH) needed → `godot-compute`
    - 3D VFX needed → `godot-vfx`
    - 2D canvas_item shader effects on existing nodes → `godot-shader-canvasitem-fx`
    - general GDScript glue after VFX is built → `godot-gdscript`
    - reusable skill-process failure discovered → `godot-retro`

## Multi-Node VFX Composition

Complex 2D effects often combine multiple building blocks. The recommended composition pattern:

### Scene Structure

```text
SlashVFX (Node2D)              # root, positioned at attack origin
├─ TrailLine (Line2D)          # premium authored trail with gradient
├─ Sparks (GPUParticles2D)     # burst of small particles
├─ SlashSprite (Sprite2D)      # slash texture, Tween-animated
└─ ScreenFlash (CanvasLayer)   # full-screen flash on dedicated layer
   └─ FlashRect (ColorRect)    # shader-driven screen effect
```

### Composition Rules

- Use **CanvasGroup** when multiple translucent children must merge as one image before fading or tinting the whole bundle. Without it, overlapping translucent children produce unintended opacity buildup.
- Use **CanvasLayer** for screen-space effects that must sit above or below specific scene layers regardless of Z-index.
- Use **SubViewport** when the VFX needs a different resolution, persistent accumulation buffer, or isolation from the main scene's rendering.
- Trigger composition from **script** (for gameplay-driven timing) or **AnimationPlayer call tracks** (for authored timelines). For simple sequences, a Tween chain with `tween_callback()` is sufficient.
- Clean up ephemeral VFX nodes with `queue_free()` via `tween_callback()` or the `finished` signal.

## Output

Leave behind bounded VFX work with:

- files, scenes, or resources created or changed
- project surfaces inspected before editing
- art direction identified (pixel-art, HD 2D, stylized, hybrid)
- tool selection rationale (why particles vs Tween vs trail vs screen-space)
- effect structure: elements, composition, trigger mechanism
- material and shader decisions with rationale
- optimization measures applied and overdraw assessment
- MCP sync or LSP steps used when editor-connected
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The exact target scenes, nodes, CanvasLayer structure, and viewport settings were inspected before building the effect.
- Art direction was identified and the correct pipeline (pixel-art vs HD 2D vs stylized vs hybrid) was followed.
- The right tool was selected: Tween for lightweight property animation, particles for emission-centric effects, Line2D/trails for streaks, screen-space shaders for camera polish.
- GPUParticles2D is used for any system exceeding ~50 concurrent particles; CPUParticles2D only for low-end fallback or GPU-bottlenecked scenes.
- `visibility_rect` is set tightly on all GPUParticles2D nodes.
- Overdraw is assessed: transparent particle layers counted at peak density, mitigated by reducing particle size, count, or lifetime.
- 2D batching is protected: materials shared, blend modes grouped, texture state consistent, Z-index fragmentation minimized.
- Pixel-art effects use nearest filtering, lossless imports, integer scaling, and transform snapping throughout.
- Screen-space effects are isolated to dedicated CanvasLayer nodes and do not interfere with HUD readability.
- CanvasGroup is used when multiple translucent VFX children must merge as one image.
- Tween-based VFX use one writer per property per effect phase, store baseline values, and kill previous tweens before starting new ones.
- Validation claims match the real operating mode.

## Failure Modes

- Do not build VFX without identifying the art direction first.
- Do not use CPUParticles2D for high-count systems — GPUParticles2D is the documented default.
- Do not ignore overdraw on transparent particle systems — 2D has no depth buffer; everything is painter's algorithm.
- Do not rely on `amount_ratio` for performance savings — it does not reduce GPU resource cost.
- Do not mix nearest and linear filter overrides within the same VFX layer — it breaks batches.
- Do not give every emitter its own unique material — share materials and use `instance uniform` for per-entity variation.
- Do not build polygon-per-segment trails when draw-call efficiency matters — polygons are unbatchable in the current RD renderer.
- Do not stack multiple screen-reading effects without understanding BackBufferCopy behavior — later readers see the same frozen source unless you intervene.
- Do not animate the same property with multiple Tweens simultaneously — the last created tween wins and causes jitter.
- Do not assume smooth interpolation is correct for pixel art — stepped motion with disabled interpolation is often the right choice.
- Do not claim visual or runtime validation when only filesystem inspection was performed.
- Do not drift into general shader education, rendering theory, or broad pipeline architecture.

## References

Read only as needed:

- `references/particle-systems-2d.md` — GPUParticles2D, CPUParticles2D, ParticleProcessMaterial in 2D, custom particle shaders, flipbook animation, emission shapes, collision, Light2D interaction
- `references/screen-space-and-compositing.md` — hint_screen_texture, BackBufferCopy, CanvasGroup, SubViewport, full-screen post-processing, multi-pass stacking, HDR 2D
- `references/trails-and-procedural.md` — Line2D trails, GPUParticles2D trails, custom _draw() trails, Tween-based procedural VFX, composition choreography, AnimationPlayer thresholds
- `references/optimization-2d.md` — overdraw, batching, blend modes, pixel-art constraints, particle budgets, trail performance, 60fps checklist
- `../../foundation/procedural-noise-and-sdf-library.md` — shared GLSL building blocks: hash, noise (value/Perlin/voronoi/cellular/wavelet), FBM, SDF 2D primitives, blend modes, domain warp utilities — use for texture-free procedural VFX shaders and screen-space effects
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
iven_performance_methodology.md`
