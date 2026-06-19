---
name: godot-compute
description: |
  Build GPU-driven compute shader systems in Godot 4 using the RenderingDevice API for
  general-purpose GPU computation: boids/flocking, SPH fluid simulation, spatial hashing,
  GPU-driven culling, procedural mesh generation, terrain generation and erosion,
  and any system requiring inter-element communication or raw compute dispatch.
  Use when the user needs RenderingDevice compute shaders, storage buffers (SSBOs),
  compute pipelines, workgroup dispatch, GPU↔CPU synchronization, ping-pong buffers,
  uniform grids, spatial hashing, neighbor search, radix sort, prefix sum,
  boids flocking, SPH fluid, GPU-driven culling, indirect draw, indirect dispatch,
  MultiMesh indirect mode, Texture2DRD, compute-to-particle bridges, compute-to-spatial
  shader bridges, procedural mesh generation, terrain heightmap generation, hydraulic erosion,
  thermal erosion, or real-time terrain modification.
  Also use when someone says "compute shader," "GPGPU," "GPU simulation," "storage buffer,"
  "SSBO," "RenderingDevice," "compute dispatch," "workgroup," "boids on GPU,"
  "fluid simulation," "SPH," "spatial hash," "GPU particles with interaction,"
  "GPU-driven rendering," "indirect draw," "procedural terrain," "GPU erosion,"
  "compute pipeline," or asks to "make particles interact with each other,"
  "simulate fluid," "generate terrain on GPU," "do neighbor search on GPU,"
  or "build a GPU-driven system."
  Do NOT use for standard GPUParticles3D/ParticleProcessMaterial work without inter-particle
  communication — that belongs to godot-vfx. Do NOT use for spatial shader authoring
  (water, terrain rendering, character shaders) — that belongs to godot-shader-spatial.
  Do NOT use for 2D canvas_item shader effects — that belongs to godot-shader-canvasitem-fx.
  Do NOT use for game feel/juice — that belongs to godot-feel.
---

# Godot Compute

Build GPU-driven systems using Godot 4's RenderingDevice compute shaders for general-purpose GPU computation, inter-element communication, and GPU-driven rendering pipelines.

Read `references/rd-compute-fundamentals.md` first. Read the appropriate reference for the task domain before implementation.

## Responsibility

- Inspect the actual project surfaces the compute system will integrate with before editing:
  - target scenes and the nodes where compute output will be consumed
  - existing compute shaders, RenderingDevice usage, and buffer conventions in the project
  - the rendering pipeline: Forward+ or Mobile (compute requires RenderingDevice-backed renderers, not Compatibility)
  - the data flow: what produces input, what consumes output, where does the CPU need visibility
  - performance context: target platform, particle/element counts, frame budget constraints
  - whether the task genuinely needs raw compute or can be solved with GPUParticles3D (route to `godot-vfx`)
- Create or update bounded compute shader work covering:
  - RenderingDevice resource lifecycle: shader compilation, pipeline creation, buffer/texture allocation, uniform sets
  - Storage buffer (SSBO) design for large datasets, uniform buffers for small parameter blocks, push constants for tiny per-dispatch scalars
  - Compute dispatch: direct dispatch with calculated workgroup counts, indirect dispatch from GPU-written buffers
  - GPU↔CPU synchronization: `compute_list_add_barrier()` between dependent passes, avoiding `sync()` on the hot path, double-buffered ping-pong state
  - Spatial data structures: uniform grid, spatial hashing, GPU sorting, prefix sum for neighbor search
  - Simulation algorithms: boids/flocking, SPH fluid (2D and 3D), force accumulation, integration
  - Rendering integration: MultiMesh indirect mode, Texture2DRD/Texture3DRD bridges, compute→particle shader, compute→spatial shader, procedural mesh generation
  - Terrain generation: heightmap synthesis, hydraulic and thermal erosion, real-time brush modification
- Keep compute systems production-safe:
  - validate device limits at init via `limit_get()` — do not hardcode workgroup sizes or buffer limits
  - pre-create all uniform sets at initialization, not per-frame
  - keep the main simulation loop GPU-resident — no `buffer_get_data()` or `sync()` on the hot path
  - use `capture_timestamp()` and debug labels for profiling, not guesswork
  - name every buffer and texture with `set_resource_name()` for RenderDoc visibility
  - free all RIDs explicitly — RenderingDevice resources are not garbage collected
- Use direct file edits for `.glsl`, `.gd`, `.tres`, and supporting files. When editor-connected, use MCP scene tools for scene assembly and node configuration.
- Validate with the strongest honest method available and report performance or synchronization limits without bluffing.

## Use When

- The project needs raw GPU compute: inter-element communication, neighbor queries, GPU-driven culling, or general-purpose GPU computation.
- Boids, flocking, SPH fluid, or any simulation requiring per-element neighbor interaction.
- GPU-driven rendering pipelines: indirect draw, MultiMesh indirect mode, GPU-written instance counts.
- Procedural mesh generation on the GPU (marching cubes, wave function collapse, etc.).
- Terrain generation, erosion simulation, or real-time terrain modification via compute.
- Spatial hashing, uniform grids, GPU sorting, or prefix sum as building blocks.
- Indirect compute dispatch where workload size is determined by a prior GPU pass.
- Any system where `GPUParticles3D` is insufficient because particles need to read each other's state.

## Do Not Use When

- Standard GPUParticles3D with ParticleProcessMaterial or custom particle shaders is sufficient — route to `godot-vfx`. The boundary is inter-particle communication: if particles only need attractors, collision, and sub-emitters, `godot-vfx` handles it.
- The task is spatial shader authoring for rendering surfaces (water, terrain materials, character shaders) — route to `godot-shader-spatial`. Note: this skill handles compute-side terrain *generation*; the rendering material is a separate concern.
- The task is 2D canvas_item shader effects — route to `godot-shader-canvasitem-fx`.
- The task is game feel/juice feedback — route to `godot-feel`.
- The task is general Compositor Effects architecture not driven by compute — route to `godot-shader-spatial`.
- Architecture or scope is still undecided — route to `godot-architect` or `godot-scope`.

## Workflow

1. Read `references/rd-compute-fundamentals.md` and the domain-appropriate reference, then inspect the project surfaces:
   - the scene and nodes where compute output will be consumed (MultiMeshInstance3D, MeshInstance3D with ShaderMaterial, GPUParticles3D, custom RD draw)
   - existing compute shaders, buffer layouts, and RenderingDevice patterns in the project
   - the renderer (Forward+ or Mobile — Compatibility has no RenderingDevice)
   - the data flow: CPU→GPU input, GPU-only simulation, GPU→render output, any CPU readback needs
   - target platform and known performance constraints
   In editor-connected mode, use `search_symbols` or `get_hover_info` to resolve Godot API questions.

2. Confirm the task stays inside the skill boundary. If the task is standard particle effects without inter-particle communication, route to `godot-vfx`. If the task is terrain *rendering* (splatting, triplanar), route to `godot-shader-spatial`.

3. Design the compute architecture:
   - identify the pass graph: what compute passes run, in what order, with what dependencies
   - choose buffer strategy: SSBOs for large arrays, UBOs for small params, push constants for tiny scalars (≤128 bytes)
   - choose synchronization: `compute_list_add_barrier()` between dependent passes, ping-pong buffers for read/write separation
   - choose rendering integration: MultiMesh indirect for homogeneous instancing, Texture2DRD for texture-based bridges, custom RD draw for heterogeneous geometry
   - choose device: main RD (`RenderingServer.get_rendering_device()`) for anything that feeds rendering, local RD only for offline/background compute
   - validate device limits: `limit_get()` for max workgroup size, max workgroup count, max push constant size, max storage buffers per set

4. Implement the compute system:
   - write GLSL compute shaders as `.glsl` files (not runtime strings — enables pipeline precompilation)
   - create the resource lifecycle in GDScript or GDExtension: shader→pipeline→buffers→uniform sets
   - pre-create all uniform sets at initialization, including ping-pong pairs
   - record compute lists with proper barrier placement between dependent passes
   - for indirect dispatch: create the dispatch buffer with `STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT`, write `(group_x, group_y, group_z)` as 3×uint32 at offset 0
   - for MultiMesh indirect: allocate with `use_indirect = true`, get buffer RIDs via `multimesh_get_buffer_rd_rid()` and `multimesh_get_command_buffer_rd_rid()`
   - for Texture2DRD bridges: create texture with `TEXTURE_USAGE_STORAGE_BIT | TEXTURE_USAGE_SAMPLING_BIT`, assign RID to `Texture2DRD.texture_rd_rid`
   - free all RIDs in `_notification(NOTIFICATION_PREDELETE)` or equivalent cleanup

5. When editor-connected, call `refresh_filesystem` after changing `.glsl`, `.tres`, `.gd`, or `.tscn` files. Call `save_scene` after MCP scene mutation.

6. Validate with the strongest honest method:
   - `get_diagnostics` on changed scripts
   - `run_scene` to verify runtime behavior — compute errors often only surface at dispatch time
   - `editor_screenshot` after `run_scene` to visually verify compute output reaching the renderer
   - check: does the simulation produce correct results? does the rendering integration work? is the frame time within budget?
   - use `capture_timestamp()` to measure actual GPU time per pass, not just total frame time

7. Escalate instead of improvising:
   - broad scope or compute design direction undecided → `godot-scope`
   - rendering architecture or pipeline decisions open → `godot-architect`
   - standard particle effects without inter-particle communication → `godot-vfx`
   - terrain rendering material (splatting, triplanar, water) → `godot-shader-spatial`
   - 2D shader effects → `godot-shader-canvasitem-fx`
   - game feel feedback wiring → `godot-feel`
   - general GDScript glue after compute system is built → `godot-gdscript`
   - reusable skill-process failure discovered → `godot-retro`

## Output

Leave behind bounded compute work with:

- files, scenes, or resources created or changed
- project surfaces inspected before editing
- compute architecture: pass graph, buffer layout, synchronization strategy
- device and renderer requirements stated
- rendering integration path chosen with rationale
- device limits validated at init
- profiling performed with GPU timestamps where applicable
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The renderer requirement (Forward+ or Mobile) was confirmed before building.
- Device limits were queried via `limit_get()` at initialization, not hardcoded.
- All uniform sets are pre-created at initialization, not rebuilt per-frame.
- The hot-path simulation loop has no `buffer_get_data()`, `sync()`, or CPU readback.
- Dependent compute passes are separated by `compute_list_add_barrier()` or separate compute lists.
- Ping-pong buffers are used when a pass both reads and writes the same logical dataset.
- GLSL compute shaders are stored as `.glsl` files, not runtime strings, to enable pipeline precompilation.
- All RenderingDevice RIDs are explicitly freed in cleanup — no leaked GPU resources.
- Buffers and textures are named via `set_resource_name()` for debugger visibility.
- GPU timestamps via `capture_timestamp()` are used to verify performance claims.
- Validation claims match the real operating mode.

## Failure Modes

- Do not use the Compatibility renderer — it has no RenderingDevice and no compute support.
- Do not call `sync()` or `buffer_get_data()` on the hot path — these stall the GPU pipeline.
- Do not create uniform sets every frame — create them once at init and reuse.
- Do not hardcode workgroup sizes without checking `limit_get()` — mobile devices have different limits.
- Do not read and write the same buffer in a single pass that depends on neighbor values — use ping-pong.
- Do not use a local RenderingDevice for compute that feeds rendering — the main RD is required for zero-copy integration.
- Do not use runtime shader string compilation for shipping builds — use `.glsl` files for precompilation.
- Do not assume `barrier()` or `full_barrier()` do anything — they are deprecated no-ops. Use `compute_list_add_barrier()`.
- Do not ignore the frame ordering constraint for compute→particle bridges: particle update runs before viewport draw, so CompositorEffect is too late for same-frame particle consumption.
- Do not claim visual or runtime validation when only filesystem inspection was performed.
- Do not drift into general shader education, rendering theory, or broad pipeline architecture.

## References

Read only as needed:

- `references/rd-compute-fundamentals.md` — RenderingDevice lifecycle, buffer strategy, synchronization, ping-pong pattern, device limits
- `references/workgroup-and-performance.md` — workgroup sizing, occupancy, shared memory, bank conflicts, profiling
- `references/spatial-data-structures.md` — uniform grid, spatial hashing, GPU sorting, prefix sum, neighbor search
- `references/simulation-patterns.md` — boids/flocking, SPH fluid (2D and 3D), force accumulation, integration
- `references/rendering-integration.md` — MultiMesh indirect, Texture2DRD, compute→particle, compute→spatial, procedural mesh, indirect dispatch
- `references/terrain-generation.md` — heightmap synthesis, erosion, real-time modification, Texture2DRD terrain bridge
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
