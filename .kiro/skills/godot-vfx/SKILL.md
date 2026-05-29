---
name: godot-vfx
description: |
  Create, configure, and optimize 3D visual effects in Godot 4 using GPUParticles3D,
  ParticleProcessMaterial, custom particle shaders, spatial VFX shaders, sub-emitters,
  trails, volumetric fog integration, and VFX-motivated screen-space effects.
  Covers both photorealistic VFX (smoke, fire, explosions, volumetric fog, PBR participating media)
  and stylized/anime VFX (cel-shaded particles, flipbook animation, impact frames, speed lines,
  Yutapon cubes, stepped shading, framerate modulation).
  Also covers VFX spatial shader techniques: depth-buffer intersection effects (shield contact glow,
  force-field intersection), screen-space distortion with mipmap blur, hologram/glitch/sci-fi energy
  shaders, and Compositor Effects for global VFX passes.
  Use when the user wants to build particle effects, explosions, magic spells, fire, smoke,
  energy shields, laser beams, shockwaves, weather effects, environmental atmosphere,
  projectile trails, debris, sparks, holograms, glitch effects, force fields,
  intersection glow, scanning effects, or any 3D visual effect.
  Also use when someone mentions GPUParticles3D, CPUParticles3D, ParticleProcessMaterial,
  particle shaders, sub-emitters, RibbonTrailMesh, TubeTrailMesh, GPUParticlesAttractor3D,
  GPUParticlesCollisionSDF3D, FogVolume, volumetric fog, Texture3D vector fields,
  flipbook animation, Texture2DArray, INSTANCE_CUSTOM, emit_subparticle, overdraw,
  alpha scissor on particles, alpha hash on particles, premultiplied alpha blending,
  VFX optimization, particle collision, depth-buffer VFX, shield intersection,
  or asks to "add particles," "make an explosion," "create a spell effect,"
  "add fire," "add smoke," "make this look cool," "add a hologram," "make a force field,"
  "add intersection glow," or "add VFX."
  Even if the user just says "I need an explosion" or "add some particles when the enemy dies"
  or "make a magic attack effect" or "add a shield effect" in a Godot project, this skill applies.
  Do NOT use for 2D particle effects (GPUParticles2D/CPUParticles2D) — those belong to
  godot-vfx-2d. Do NOT use for 2D canvas_item shader effects (hit flash, dissolve, outline) —
  those belong to godot-shader-canvasitem-fx. Do NOT use for game feel/juice feedback sequences
  (screen shake, hit pause, scale pops) — those belong to godot-feel.
---

# Godot VFX

Build bounded 3D visual effects using Godot 4's GPU particle systems, spatial VFX shaders, volumetric fog, and VFX-motivated screen-space techniques.

Read `references/particle-systems.md` first. Read the appropriate reference for the task domain before implementation. Read `references/optimization-guide.md` before finalizing any effect.

## Responsibility

- Inspect the actual project surfaces the VFX will integrate with before editing:
  - target scenes and the nodes where effects attach or spawn
  - existing particle systems, materials, shaders, and VFX conventions in the project
  - the art direction: photorealistic, stylized/anime, or hybrid
  - camera setup, lighting environment, and WorldEnvironment settings that affect VFX rendering
  - the gameplay trigger: what event spawns the effect, what script or signal drives it
  - performance context: target platform, existing particle budgets, known bottlenecks
- Create or update bounded 3D VFX work covering:
  - GPUParticles3D configuration and ParticleProcessMaterial setup
  - custom particle process shaders when the built-in material is insufficient
  - spatial shaders for VFX meshes (energy shields, beams, distortion planes, volumetric raymarching)
  - sub-emitter choreography for multi-stage effects
  - trail rendering via RibbonTrailMesh and TubeTrailMesh
  - particle collision and attractor setup
  - volumetric fog integration via FogVolume nodes and fog shaders
  - VFX-motivated screen-space effects (impact frames, speed lines, shockwave distortion, color correction)
  - flipbook and Texture2DArray management for animated VFX sprites
- Keep effects production-safe:
  - enforce overdraw awareness on every transparent particle system
  - use alpha scissor over alpha blending when hard edges are acceptable
  - prefer premultiplied alpha for mixed additive/blend effects
  - set visibility ranges on ambient and secondary effects
  - budget particle counts against the 16.67ms frame target
- Use direct file edits for `.gdshader`, `.tres`, `.gd`, and supporting files. When editor-connected, use MCP scene tools for scene assembly and node configuration.
- Validate with the strongest honest method available and report rendering or performance limits without bluffing.

## Use When

- The project needs a concrete 3D visual effect: particles, volumetrics, VFX shaders, or VFX screen-space techniques.
- A GPUParticles3D system needs to be created, configured, optimized, or debugged.
- The task involves particle process shaders, sub-emitter chains, trail meshes, collision volumes, or attractor fields.
- Spatial shaders for VFX meshes (shields, beams, distortion, raymarched volumes) are needed.
- VFX-motivated post-processing is required: impact frames, speed lines, shockwave distortion, dynamic color correction during gameplay events.
- Flipbook animation, Texture2DArray setup, or VRAM compression decisions for VFX textures are involved.
- Volumetric fog or FogVolume integration with particle systems is needed.

## Do Not Use When

- The task is 2D particle effects (GPUParticles2D, CPUParticles2D) — route to `godot-vfx-2d`.
- The task is 2D canvas_item shader effects (hit flash, dissolve, outline, palette swap) — route to `godot-shader-canvasitem-fx`.
- The task is game feel/juice feedback (screen shake, hit pause, scale pops) — route to `godot-feel`.
- The task requires raw RenderingDevice compute shaders for inter-particle communication (boids, SPH fluid, spatial hashing) — route to `godot-compute`. Note: standard GPUParticles3D with attractors and collision is within scope; only escalate when true inter-particle querying is needed.
- The task is general 3D spatial shader authoring for non-VFX surfaces (water, terrain, character shaders) — route to `godot-shader-spatial`.
- The task is general Compositor Effects architecture not motivated by a specific VFX need — route to `godot-shader-spatial`.
- The task is WorldEnvironment/Environment configuration (tonemap, SSAO, SSR, SDFGI settings) without a specific VFX integration need — route to `godot-architect`.
- Architecture or scope is still undecided — route to `godot-architect` or `godot-scope`.

## Art Direction Router

Before building any effect, identify the project's art direction. This determines which techniques, shading models, and reference sections apply.

### Photorealistic Pipeline
- PBR texture maps on particle billboards: albedo as single-scattering albedo, normal maps for pseudo-volumetric shape, roughness for optical sharpness, thickness for optical depth
- Smooth alpha blending with soft gradients
- HDR emission for bloom interaction
- Volumetric fog integration for atmospheric cohesion
- Dynamic lighting interaction via Forward+ clustered renderer
- Shadow casting from dense particle volumes
- Read `references/vfx-shaders.md` section on PBR participating media

### Stylized / Anime Pipeline
- Hard-edged discrete shapes, stepped color bands
- `render_mode unshaded` for pure energy effects that bypass environmental lighting
- Custom `light()` function with `step()` math for cel-shaded particles that must interact with scene lights
- Flipbook-dominant animation with procedural UV distortion overlay
- Texture2DArray with BC7 compression for crisp alpha edges
- Impact frames, speed lines, and dynamic color correction as screen-space staging
- Framerate modulation: stepped interpolation for 1s/2s/3s animation timing
- Yutapon cubes for stylized debris, Obake smears via vertex shader velocity stretching
- Read `references/stylized-vfx.md` for the full NPR pipeline

### Hybrid
- Many projects mix both. Environmental effects (fog, rain, dust) may be photorealistic while combat VFX are stylized. Identify per-effect, not per-project.

## Workflow

1. Read `references/particle-systems.md` and the art-direction-appropriate reference, then inspect the project surfaces:
   - the scene and nodes where the effect will live
   - existing VFX systems, materials, shaders, and naming conventions
   - camera, lighting, and WorldEnvironment setup
   - the gameplay event or script that triggers the effect
   - target platform and any known performance constraints
   In editor-connected mode, use `find_nodes` with type "GPUParticles3D" or "FogVolume" for targeted lookups. Use `search_symbols`, `get_definition`, or `get_hover_info` to resolve Godot API questions.

2. Confirm the effect stays inside the skill boundary. If the task needs raw compute shaders, general spatial shaders, or 2D particles, escalate. If the task needs game-feel feedback wiring, note that `godot-feel` handles the trigger timing while this skill handles the visual content.

3. Design the effect structure:
   - identify the stages: primary burst, secondary trails/smoke, tertiary debris/sparks
   - decide which stages use sub-emitters vs separate GPUParticles3D nodes triggered by script
   - choose between ParticleProcessMaterial (sufficient for most effects) and custom particle shaders (needed for CUSTOM data packing, complex emission logic, or non-standard physics)
   - choose mesh type per stage: QuadMesh billboard, custom trimmed mesh, RibbonTrailMesh, TubeTrailMesh, or full 3D mesh for debris
   - identify spatial shader needs: does any mesh need a custom ShaderMaterial for dissolve, distortion, cel-shading, or INSTANCE_CUSTOM unpacking?

4. Use MCP for scene assembly:
   - `open_scene` for the target scene
   - `scene://current/tree` to inspect hierarchy before mutation
   - `add_node` for GPUParticles3D, GPUParticlesCollisionSDF3D, GPUParticlesAttractor3D, FogVolume, and supporting nodes
   - `update_property` for particle material references, transforms, visibility ranges, draw passes, collision layers
   - `connect_signal` when VFX triggering requires signal wiring

5. Implement the effect:
   - configure ParticleProcessMaterial or write custom particle shader
   - create spatial ShaderMaterial for VFX meshes when needed
   - set up sub-emitter chains for multi-stage effects
   - configure collision (SDF bake for complex environments, analytical shapes for simple cases)
   - configure attractors for directional forces or vector field flow
   - for screen-space VFX: create dedicated CanvasLayer + ColorRect with screen-reading shader, or use Compositor Effects for global passes
   - for flipbooks: import as Texture2DArray with BC7 compression, wire frame index through INSTANCE_CUSTOM

6. Read `references/optimization-guide.md`, then apply optimization:
   - audit overdraw: count overlapping transparent layers at peak density
   - switch to alpha scissor where hard edges are acceptable
   - use premultiplied alpha for mixed additive/blend thermal effects
   - trim particle meshes to fit opaque texture bounds
   - set visibility ranges for distance culling on secondary effects
   - enable `fixed_fps` interpolation to prevent physics-tick stuttering
   - budget: aim for total transparent particle overdraw under 4x at any screen pixel

7. In editor-connected mode, call `refresh_filesystem` after changing `.gdshader`, `.tres`, `.gd`, or `.tscn` files. Call `save_scene` after MCP scene mutation.

8. Validate with the strongest honest method:
   - `get_diagnostics` on changed scripts and shaders
   - `run_scene` to verify runtime behavior (compile-clean does not mean plays correctly)
   - `editor_screenshot` after `run_scene` to visually verify the effect in the running game — this is critical for VFX where visual correctness is the primary validation
   - check: does the effect trigger at the right moment? do particles collide correctly? do sub-emitters chain properly? is overdraw within budget?

9. Escalate instead of improvising:
    - broad scope or VFX design direction undecided → `godot-scope`
    - rendering architecture or pipeline decisions open → `godot-architect`
    - scene/resource ownership of VFX nodes is risky → `godot-scene-resource`
    - inter-particle compute (boids, SPH) needed → `godot-compute`
    - general spatial shader work unrelated to VFX → `godot-shader-spatial`
    - 2D VFX needed → `godot-vfx-2d`
    - game feel feedback wiring → `godot-feel`
    - 2D canvas_item shader effects → `godot-shader-canvasitem-fx`
    - general GDScript glue after VFX is built → `godot-gdscript`
    - reusable skill-process failure discovered → `godot-retro`

## Output

Leave behind bounded VFX work with:

- files, scenes, or resources created or changed
- project surfaces inspected before editing
- art direction identified (photorealistic, stylized, hybrid)
- effect structure: stages, sub-emitter chains, mesh types, shader types
- material and shader decisions with rationale
- optimization measures applied and overdraw assessment
- MCP sync or LSP steps used when editor-connected
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The exact target scenes, nodes, lighting, and camera setup were inspected before building the effect.
- Art direction was identified and the correct pipeline (PBR vs NPR vs hybrid) was followed.
- GPUParticles3D is used for any system exceeding ~50 concurrent particles; CPUParticles3D only for micro-emissions or fallbacks.
- Sub-emitter chains are used for multi-stage effects instead of fragile GDScript timing.
- Overdraw is assessed and mitigated: alpha scissor, premultiplied alpha, mesh trimming, or visibility ranges applied where appropriate.
- Flipbook textures use Texture2DArray with BC7 compression when alpha crispness matters.
- Vector field and volumetric data textures are imported uncompressed (no S3TC/BPTC on velocity data).
- `fixed_fps` interpolation is enabled on particle systems to prevent physics-tick stuttering.
- Screen-space VFX effects are isolated to dedicated CanvasLayer nodes and do not interfere with gameplay rendering.
- Validation claims match the real operating mode.

## Failure Modes

- Do not build VFX without identifying the art direction first.
- Do not use CPUParticles3D for high-count systems — it is a legacy fallback.
- Do not ignore overdraw on transparent particle systems.
- Do not use lossy VRAM compression (S3TC/BPTC) on vector field or velocity Texture3D data.
- Do not use standard alpha blending when alpha scissor or premultiplied alpha would serve the effect and save fill rate.
- Do not build multi-stage effects with GDScript timers when sub-emitters can handle the choreography on the GPU.
- Do not stack screen-space VFX effects without considering their combined overdraw cost.
- Do not assume raw UV math works correctly on flipbook atlases without verifying frame indexing.
- Do not claim visual or runtime validation when only filesystem inspection was performed.
- Do not drift into general shader education, rendering theory, or broad pipeline architecture.

## References

Read only as needed:

- `references/particle-systems.md` — GPUParticles3D, ParticleProcessMaterial, custom particle shaders, sub-emitters, trails, collision, attractors
- `references/vfx-shaders.md` — spatial shaders for VFX meshes, vertex displacement, refraction, screen-reading, mipmap blur, depth-buffer intersection effects, INSTANCE_CUSTOM, cel-shading, raymarching, hologram/glitch/sci-fi energy shaders
- `references/volumetric-and-fog.md` — volumetric fog, FogVolume, fog shaders, Texture3D, offline simulation data import
- `references/stylized-vfx.md` — anime/NPR pipeline, flipbooks, Texture2DArray, stepped animation, impact frames, speed lines, smears, color correction
- `references/optimization-guide.md` — overdraw, alpha scissor, alpha hash, alpha-to-coverage, premultiplied alpha, mesh trimming, draw calls, visibility ranges, Compositor Effects API patterns, VRAM compression, shader warm-up
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
