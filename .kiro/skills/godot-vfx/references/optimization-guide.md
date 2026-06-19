# VFX Optimization Guide

Overdraw mitigation, alpha strategies, mesh trimming, draw call management, visibility ranges, Compositor Effects, VRAM compression, and performance budgeting for 3D VFX in Godot 4.

## Table of Contents

1. [The Overdraw Problem](#overdraw)
2. [Alpha Strategies](#alpha-strategies)
3. [Mesh Trimming](#mesh-trimming)
4. [Draw Call Management](#draw-calls)
5. [Visibility Ranges and Culling](#visibility)
6. [VRAM and Texture Optimization](#vram)
7. [Compositor Effects for Global VFX](#compositor)
8. [Performance Budget Framework](#budget)
9. [Profiling Checklist](#profiling)

---

## 1. The Overdraw Problem <a name="overdraw"></a>

Overdraw is the single most catastrophic performance bottleneck in real-time VFX.

### Why Overdraw Kills Performance

Opaque rendering benefits from a depth prepass: the GPU populates the Z-buffer first, then discards occluded fragments before running the expensive fragment shader. Transparent particles cannot use this optimization — the background must remain visible through them.

Transparent particles are rendered back-to-front (painter's algorithm), sorted by Node3D position. If 50 overlapping smoke billboards cover the same screen pixel, the GPU executes the fragment shader 50 times for that pixel — evaluating clustered lighting, shadow maps, and texture fetches each time.

### Measuring Overdraw

Overdraw factor = number of transparent fragment shader executions per screen pixel at peak density.

- 1–2x: acceptable for most effects
- 3–4x: budget limit for hero effects
- 5x+: performance emergency — will tank frame rate on mid-range hardware

Estimate by counting the maximum number of overlapping transparent particles visible from the camera's perspective at the effect's peak intensity.

### Reducing Overdraw

The strategies below are ordered by impact. Apply them in combination.

---

## 2. Alpha Strategies <a name="alpha-strategies"></a>

### Alpha Scissor (Alpha Testing)

Replace alpha blending with alpha scissor wherever hard edges are acceptable.

```glsl
// In spatial shader
ALPHA_SCISSOR_THRESHOLD = 0.5;
```

Or set `alpha_scissor_threshold` on StandardMaterial3D / ParticleProcessMaterial.

**What it does:** Fragments below the threshold are discarded entirely. The particle becomes opaque for rendering purposes.

**Benefits:**
- Writes to the depth buffer → enables early-Z culling
- Eliminates transparency sorting entirely
- Allows standard shadow casting
- Massively reduces overdraw

**Trade-off:** Hard, non-aliased pixel edges instead of soft transparency. Acceptable for:
- Stylized/anime effects (hard edges are the aesthetic)
- Dense smoke at distance (edges not visible)
- Debris and solid particles
- Any effect where soft edges are not critical

### Premultiplied Alpha Blending

New in Godot 4.3. Pre-multiplies color by alpha before blending.

```glsl
render_mode blend_premul_alpha;
```

**What it does:** A single material exhibits both additive blending (bright pixels) and standard mix blending (dark pixels) simultaneously.

**Benefits:**
- Halves particle count for mixed-energy effects (fire core + smoke body in one system)
- Reduces total overdraw by combining what previously required two separate particle systems
- No visible blend seam between additive and mix regions

**Use for:** Fire, explosions, magical energy — any effect with both bright emissive and dark absorptive regions.

### Alpha Blending (Standard)

Standard back-to-front sorted transparency. Use only when:
- Soft, gradual edges are visually critical
- The effect has low particle density (few overlapping layers)
- The effect is a hero element where visual quality justifies the cost

### Alpha Hash (Stochastic Transparency)

Uses `ALPHA_HASH_SCALE` to dither transparency stochastically. Each fragment is either fully opaque or fully discarded based on a hash of its screen position and alpha value.

```glsl
// In spatial shader
ALPHA_HASH_SCALE = texture(alpha_tex, UV).r;
```

Or set `transparency = TRANSPARENCY_ALPHA_HASH` on StandardMaterial3D.

**What it does:** Approximates soft transparency without sorting. The dither pattern resolves into smooth coverage at distance or with MSAA.

**Benefits:**
- No transparency sorting needed — stays in the opaque pipeline
- Writes to the depth buffer → enables early-Z culling and shadow casting
- Softer apparent coverage than alpha scissor without the cost of alpha blend
- Benefits from automatic instancing in Forward+ (alpha blend does not)

**Trade-off:** Visible dither noise up close, especially without MSAA. Best for:
- Dense smoke and volumetric-looking particle clouds
- Hologram/energy effects that need partial transparency without sorting
- Any VFX where soft coverage matters more than pixel-perfect edges

### Alpha-to-Coverage (MSAA Edge Quality)

When using Alpha Scissor or Alpha Hash with MSAA enabled, alpha-to-coverage converts the alpha value into a coverage mask across MSAA sub-samples. This produces smoother edges than raw scissor at no extra shader cost.

```glsl
render_mode blend_mix, depth_draw_opaque, cull_disabled, alpha_to_coverage;
```

Or enable `alpha_antialiasing_mode` on StandardMaterial3D (Alpha Edge Blend or Alpha Edge Clip).

**Requires:** MSAA enabled (2× minimum, 4× recommended). Without MSAA, alpha-to-coverage has no effect.

**Use for:** Stylized VFX particles that use alpha scissor but need cleaner edges — flipbook smoke, cel-shaded energy, debris with alpha cutouts.

### Depth Pre-Pass

When alpha blending is mandatory but sorting artifacts are visible:

Enable `depth_draw_alpha_prepass` on the material. This forces an initial rendering pass of opaque pixels to populate the depth buffer, improving transparency sorting accuracy.

**Cost:** Vertex shader executes twice for the geometry. Reserve for hero effects where sorting errors are highly visible.

**Instancing caveat:** In Forward+, alpha-blended and depth-pre-pass materials do **not** benefit from automatic instancing. If you use depth pre-pass on high-count particle systems, you lose one of Forward+'s cheapest batching paths. Prefer alpha scissor or alpha hash for volume-heavy effects.

---

## 3. Mesh Trimming <a name="mesh-trimming"></a>

### The Problem

A standard QuadMesh renders a full rectangle. If the particle texture is circular (like most smoke puffs), ~21% of the quad area is fully transparent. The GPU still evaluates the fragment shader for those empty pixels.

At 10,000 particles, that's 2,100 particles' worth of wasted fragment shader invocations.

### The Solution

Replace QuadMesh with a custom mesh that closely fits the opaque bounds of the texture:
- **Octagon**: cuts ~17% of wasted area vs quad, negligible vertex cost increase
- **Custom cutout**: trace the texture's alpha boundary, simplify to 6–12 vertices

### When to Trim

Trim when:
- The particle texture has >20% transparent area on the quad
- The system has >100 concurrent particles
- The effect is a persistent ambient system (dust, fog, rain) that runs continuously

Skip trimming when:
- The texture fills most of the quad
- The system has <50 particles
- The effect is a brief one-shot burst

---

## 4. Draw Call Management <a name="draw-calls"></a>

### Draw Call Cost

Every unique object or distinct material drawn requires a draw call — a CPU command instructing the GPU what to render. High draw call counts bottleneck the CPU.

### GPUParticles3D Draw Calls

A single GPUParticles3D node with one draw pass = one draw call for all its particles. This is inherently efficient. Draw call problems arise from:
- Many separate GPUParticles3D nodes active simultaneously
- Multiple draw passes per node (each pass = additional draw call)
- Unique materials per particle system preventing batching

### MultiMeshInstance3D for Persistent VFX

For thousands of persistent visual elements (bullet casings, environmental debris, scattered embers), use MultiMeshInstance3D instead of individual MeshInstance3D nodes:
- One draw call for all instances
- CPU sets transforms in the buffer
- GPU instances the mesh at each transform
- Combine with compute shaders for GPU-driven placement

### Reducing Draw Calls

- Consolidate similar particle systems into fewer nodes with higher particle counts
- Minimize draw passes (use only when visual benefit justifies the cost)
- Share materials across particle systems where possible
- Use MultiMeshInstance3D for persistent scattered elements

---

## 5. Visibility Ranges and Culling <a name="visibility"></a>

### Visibility Ranges (HLOD)

Configure distance thresholds on 3D nodes to swap or disable VFX based on camera distance:

- `visibility_range_begin`: distance at which the node starts appearing
- `visibility_range_end`: distance at which the node disappears
- `visibility_range_begin_margin` / `visibility_range_end_margin`: fade transition width
- `visibility_range_fade_mode`: `DISABLED`, `SELF`, or `DEPENDENCIES`

Use for:
- Disabling complex shader-heavy particle systems at distance
- Swapping high-fidelity effects for simplified versions
- Preventing sub-pixel particle rendering (particles too small to see still cost GPU time)

### Frustum Culling

Godot automatically culls objects outside the camera's field of view. Ensure `visibility_aabb` on GPUParticles3D is set tightly — an oversized AABB prevents culling when the emitter is off-screen but the AABB still intersects the frustum.

### Occlusion Culling

For indoor environments, use OccluderInstance3D nodes to prevent rendering VFX hidden behind walls or large structures. The engine generates a simplified depth buffer from occluders and culls hidden objects before draw calls are issued.

### Distance Fade on Lights

VFX-spawned lights (OmniLight3D, SpotLight3D) should use distance fade:
- `distance_fade_enabled = true`
- `distance_fade_begin`: start fading
- `distance_fade_length`: fade to zero over this distance

This allows the clustered renderer to drop distant VFX lights from the evaluation grid.

---

## 6. VRAM and Texture Optimization <a name="vram"></a>

### Compression Selection

| Texture Type | Recommended Compression | Rationale |
|-------------|------------------------|-----------|
| Particle sprite (photorealistic) | VRAM Compressed (Standard) S3TC | Soft gradients tolerate compression |
| Particle sprite (anime, hard alpha) | **VRAM Compressed (High Quality) BC7** | Preserves crisp alpha edges |
| Flipbook Texture2DArray | **BC7** | Independent slice mipmaps + alpha quality |
| Vector field Texture3D | **Uncompressed (16/32-bit float)** | Compression corrupts velocity data |
| SDF Texture3D | **Uncompressed (16/32-bit float)** | Compression corrupts distance data |
| Noise texture (visual) | VRAM Compressed OK | Visual use tolerates artifacts |
| Normal map for particles | VRAM Compressed (Standard) | Standard normal map compression |

### Mipmap Strategy

Always generate mipmaps for VFX textures:
- ~33% VRAM increase
- Drastically reduces memory bandwidth for distant particles
- Prevents cache thrashing when full-resolution textures are sampled for sub-pixel particles

Exception: vector field and SDF Texture3D data may not benefit from mipmaps if always sampled at full resolution.

### Channel Packing

Pack multiple grayscale data maps into a single RGBA texture:
- R: dissolve noise
- G: distortion strength
- B: emission mask
- A: alpha shape

One texture fetch instead of four. Significant bandwidth savings on high-count particle systems.

---

## 7. Compositor Effects for Global VFX <a name="compositor"></a>

### The Problem with Per-Particle Screen Reading

Screen-reading shaders (`hint_screen_texture`) on individual particle meshes are expensive when many particles overlap. If 20 shockwave particles each read and distort the screen buffer, the GPU reads and processes the screen 20 times.

### Compositor Solution

The Compositor Effects API (Godot 4.3+) allows injecting custom compute shaders into the render pipeline as global passes.

**Pipeline:**
1. Particles emit distortion data (velocity, strength) into a low-resolution custom buffer
2. The Compositor captures the main color buffer
3. A single full-screen compute pass processes the distortion buffer against the color buffer
4. Result: one distortion pass regardless of particle count

**Benefits:**
- Decouples distortion cost from particle count
- Single full-screen pass instead of per-particle screen reads
- Maintains 60fps even during massive screen-filling effects

### When to Use Compositor Effects

- Global heat haze or distortion from many simultaneous sources
- Custom bloom or glow processing for VFX (per-mip tinting for stylized effects)
- Full-screen color grading tied to VFX events
- Any screen-space effect that would otherwise require per-particle screen reading

### When NOT to Use

- Simple single-source distortion (one shockwave) — per-mesh shader is simpler and sufficient
- Effects that don't read the screen buffer
- Mobile targets — Compositor is documented for Forward+ and Mobile, but compute-based image paths have known issues on Mobile in some Godot versions. Validate early on Mobile.

### Production API Patterns for VFX Compositor Effects

`CompositorEffect` runs on the rendering thread. The callback receives `RenderData`, from which you obtain scene buffers and camera data. The API is marked **experimental** — it works but may shift across Godot 4.x releases.

**Pipeline stage:** Use `EFFECT_CALLBACK_TYPE_POST_TRANSPARENT` for VFX compositing. This runs after all opaque and transparent rendering but before built-in post-processing and output — the correct position for VFX screen effects.

**Buffer access flags:**
- `access_resolved_color = true` — resolves MSAA before your effect, safe to sample
- `access_resolved_depth = true` — resolved depth for depth-aware VFX compositing
- `needs_motion_vectors = true` — velocity buffer for motion-aware VFX (Forward+ and Mobile)
- `needs_normal_roughness = true` — normal/roughness buffer (**Forward+ only**)

**Custom texture management:** Use `RenderSceneBuffersRD.create_texture()` with a custom context name (e.g., `&"vfx_distortion"`) to allocate VFX-owned intermediate textures. The buffer manager caches by `(context, name)`, so requesting the same texture each frame reuses the existing RID if the viewport hasn't changed. For multi-pass VFX (e.g., downsample → blur → composite), allocate ping-pong chains in your custom context.

**Compute dispatch pattern:**
```gdscript
# Inside _render_callback():
var rsb: RenderSceneBuffersRD = render_data.get_render_scene_buffers()
var color_rid: RID = rsb.get_texture("render_buffers", "color")
var size: Vector2i = rsb.get_internal_size()

# Use UniformSetCacheRD to avoid leaking uniform sets.
var uniform := RDUniform.new()
uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
uniform.binding = 0
uniform.add_id(color_rid)
var uniform_set := UniformSetCacheRD.get_cache(shader, 0, [uniform])

# Dispatch compute.
var compute_list := rd.compute_list_begin()
rd.compute_list_bind_compute_pipeline(compute_list, pipeline)
rd.compute_list_bind_uniform_set(compute_list, uniform_set, 0)
rd.compute_list_set_push_constant(compute_list, push_data, push_data.size())
rd.compute_list_dispatch(compute_list, ceili(size.x / 8.0), ceili(size.y / 8.0), 1)
rd.compute_list_end()
```

**VFX-specific design rule:** Events do not create compositor effects; events feed data into persistent compositor effects. A hit event adjusts distortion intensity; a dash event raises motion-blur weight. The compositor stack remains persistent, warm, and renderer-owned. This avoids shader cold starts and keeps the VFX pipeline predictable.

---

## 8. Performance Budget Framework <a name="budget"></a>

### Frame Budget

At 60fps, total frame time = 16.67ms. VFX should consume no more than 2–4ms of GPU time in a typical scene.

### Budget Guidelines

| Category | Budget | Notes |
|----------|--------|-------|
| Hero effect (explosion, ultimate) | 1–2ms GPU | Temporary spike, acceptable |
| Ambient particles (dust, fog, rain) | 0.5–1ms GPU | Persistent, must be cheap |
| Screen-space VFX (speed lines, impact) | 0.2–0.5ms GPU | Full-screen shader cost |
| Total VFX budget | 2–4ms GPU | Leaves headroom for gameplay, AI, physics |

### Particle Count Guidelines

These are rough guidelines — actual budgets depend on shader complexity, mesh size, and overdraw:

| Effect Type | Suggested Max Particles | Rationale |
|-------------|------------------------|-----------|
| Dense smoke cloud | 50–200 | High overdraw, large billboards |
| Spark shower | 200–1000 | Small particles, low overdraw |
| Debris (Yutapon cubes) | 50–200 | 3D meshes, lit, moderate cost |
| Rain/snow | 500–2000 | Small, simple shader, low overdraw |
| Ambient dust | 100–500 | Persistent, must be cheap |
| Trail particles | 50–200 per trail | Ribbon mesh is more efficient for continuous trails |

### Overdraw Budget

Target: maximum 4x transparent overdraw at any screen pixel during peak effect intensity.

Strategies to stay within budget:
1. Alpha scissor on everything that tolerates hard edges
2. Premultiplied alpha for mixed additive/blend effects
3. Trim particle meshes
4. Reduce particle count and increase individual particle size
5. Use shorter lifetimes to reduce accumulation
6. Sub-Viewport downscaling for extremely dense effects (render at half resolution, upscale)

### Sub-Viewport Downscaling

For effects that exceed the overdraw budget:
1. Render the particle system into an isolated SubViewport at half or quarter resolution
2. Upscale the result onto a screen-space quad
3. Drastically cuts fill rate cost

**Trade-offs:**
- Requires depth composition math to blend correctly with the main scene
- Linear color space and HDR must match between SubViewport and main camera
- Adds complexity — reserve for effects that genuinely need it

---

## 9. Profiling Checklist <a name="profiling"></a>

Before shipping any VFX, verify:

- [ ] **Overdraw**: estimate peak transparent layer count at the densest screen region
- [ ] **Alpha strategy**: alpha scissor used wherever hard edges are acceptable
- [ ] **Mesh trimming**: particle meshes trimmed if >20% transparent area
- [ ] **Visibility ranges**: secondary/ambient effects have distance culling
- [ ] **AABB**: `visibility_aabb` set tightly on all GPUParticles3D nodes
- [ ] **Fixed FPS**: `fixed_fps` set and `interpolate` enabled
- [ ] **Texture compression**: BC7 for anime flipbooks, uncompressed for vector/SDF data
- [ ] **Mipmaps**: generated for all VFX textures
- [ ] **Draw passes**: minimized (only use multiple when visually justified)
- [ ] **VFX lights**: distance fade enabled on spawned OmniLight3D/SpotLight3D
- [ ] **Screen reading**: Compositor used if multiple simultaneous distortion sources exist
- [ ] **Channel packing**: grayscale data packed into RGBA channels where possible
- [ ] **Shader warm-up**: VFX shaders primed before first gameplay use (see section 10)

### Godot Profiler Integration

Use the built-in profiler (Debugger → Profiler) to measure:
- GPU frame time
- Draw call count
- Vertex count
- Object count

Use the Monitors tab to track:
- `Rendering/total_draw_calls_in_frame`
- `Rendering/total_objects_in_frame`
- `Rendering/total_primitives_in_frame`

For GPU-specific profiling, use RenderDoc or platform-specific GPU profilers to identify fragment shader bottlenecks and overdraw hotspots.

---

## 10. Shader Warm-Up for VFX <a name="shader-warmup"></a>

A VFX effect that causes a visible hitch on first use is a shipping bug. Godot compiles shader pipelines on demand — the first time a material/shader combination is rendered with a specific set of renderer features, the GPU must compile the pipeline. For VFX that only appear during gameplay (explosions, ultimates, boss phase transitions), this means a stutter exactly when the player least wants it.

### Engine-Level Warm-Up (Godot 4.4+)

Since Godot 4.4, Forward+ and Mobile use **ubershaders** and **pipeline precompilation** to reduce first-seen hitches. Since Godot 4.5, the **shader baker** can bake intermediate shader formats at export time, reducing startup compilation cost.

These mechanisms work automatically for materials the engine encounters, but they depend on the engine seeing the right combination of renderer features. Features that influence pipeline compilation include:
- MSAA level
- Motion vectors enabled/disabled
- Normal/roughness buffer (Forward+ only)
- Multiview / XR

### File-Backed Shaders for Compositor Effects

The Godot compositor tutorial explicitly warns: if you build compute shader source dynamically at runtime (string assembly), Godot **cannot precompile or cache** it. File-backed `.glsl` shaders can be precompiled and cached.

**Production rule:** Prototype with runtime strings if needed, but ship with file-backed shader sources. Keep the pass graph fixed and predictable — a small set of stable pipelines, not arbitrary runtime generation.

### Feature Priming for VFX

If a VFX effect only appears later during gameplay, the engine has no opportunity to precompile its pipelines. The fix is to instantiate a hidden or off-screen version early:

- Spawn the VFX node off-screen or in a hidden `SubViewport` for at least one frame during loading
- This forces the engine to compile all required pipelines before gameplay begins
- Remove or disable the priming instance after one rendered frame

This is especially important for:
- Compositor Effects with compute shaders (custom VFX bloom, global distortion)
- Complex particle shaders with multiple render modes
- VFX that combine unusual feature sets (e.g., alpha hash + motion vectors + custom depth)

### Monitoring Pipeline Compilation

Use the debugger's pipeline-compilation monitors to detect whether new pipelines are being created during gameplay. If you see pipeline compilations during play, identify which VFX triggered them and add priming for those effects.

### Warm-Up Checklist

- Use file-backed `.glsl` for all Compositor Effect compute shaders
- Prime VFX materials during loading by rendering them off-screen for one frame
- Keep Compositor Effect pass graphs fixed — avoid runtime shader string assembly
- Monitor pipeline compilations during playtesting to catch cold starts
