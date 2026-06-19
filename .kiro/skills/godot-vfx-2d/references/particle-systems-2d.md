# 2D Particle Systems Reference

GPUParticles2D, CPUParticles2D, ParticleProcessMaterial in 2D context, custom particle shaders, flipbook animation, emission shapes, collision, and Light2D interaction.

## Table of Contents

1. [GPUParticles2D vs CPUParticles2D](#gpu-vs-cpu)
2. [ParticleProcessMaterial in 2D](#process-material)
3. [Custom 2D Particle Shaders](#custom-shaders)
4. [Flipbook Animation](#flipbook)
5. [Light2D and Normal-Mapped Particles](#lighting)
6. [Emission Shapes](#emission)
7. [Collision in 2D](#collision)
8. [Key Properties Quick Reference](#properties)

---

## 1. GPUParticles2D vs CPUParticles2D <a name="gpu-vs-cpu"></a>

### Default Choice: GPUParticles2D

GPUParticles2D keeps simulation on the GPU via ParticleProcessMaterial or custom ShaderMaterial. It is the documented default for ambitious 2D VFX. The stable manual explicitly recommends GPUParticles2D unless there is a concrete reason not to, and states there are no plans to add new features to CPUParticles2D.

GPUParticles2D advantages:
- GPU-accelerated simulation scales far better at large particle counts
- `visibility_rect` provides explicit screen-space activation bounds for culling
- Supports custom `shader_type particles` shaders
- Supports `emit_particle()` for scripted burst injection (Forward+ and Mobile renderers only, not Compatibility)

### When CPUParticles2D Is Appropriate

CPUParticles2D simulates on the CPU, serializes transform/color/custom data into a buffer, and uploads to a MultiMesh for rendering. Use it for:
- Low-end devices where the GPU is already bottlenecked by fill rate, lighting, or post-processing
- Deterministic replay or tooling workflows requiring `use_fixed_seed`, `request_particles_process()`, and physics interpolation
- Micro-emissions under ~10 particles where GPU dispatch overhead exceeds simulation cost

CPUParticles2D exposes `physics_interpolation_mode` override in current stable, which GPUParticles2D does not yet support for 2D.

### Critical Difference: amount and amount_ratio

Increasing `amount` raises GPU requirements even if many particles are off screen. Lowering `amount_ratio` has **no performance benefit** — resources are still allocated and processed for the full `amount`. If a particle system is too expensive, reduce `amount`, not `amount_ratio`. Use `amount_ratio` for effect design (visual density), not performance tuning.

### Fixed FPS Defaults

GPUParticles2D defaults `fixed_fps` to **30**. CPUParticles2D defaults it to **0** (process every frame). `Fract Delta` improves smoothness but has a performance trade-off at high counts, fast particles, or high randomness.

For pixel art, the usual answer is the opposite of the default smoothing stack: use integer positions, nearest filtering, and often **disable** interpolation and Fract Delta for deliberately stepped motion.

---

## 2. ParticleProcessMaterial in 2D <a name="process-material"></a>

ParticleProcessMaterial is shared across 2D and 3D. In 2D, it works well but thinks in partially 3D terms.

### 3D-First API Quirks

- Emission direction is a **Vector3** — treat XY as authoritative, keep Z neutral
- `Flatness` is only useful for 3D particles — ignore it in 2D
- CPUParticles2D `ROTATE_Y` and `DISABLE_Z` flags exist for consistency but are unused in 2D

### Key Material Properties for 2D

| Property | 2D Guidance |
|----------|-------------|
| `direction` + `spread` | Emission cone — use XY components only |
| `initial_velocity_min/max` | Speed at birth in pixels/second |
| `gravity` | Constant acceleration — `(0, 980, 0)` for downward gravity in pixels |
| `damping` / `damping_as_friction` | Friction damping is snappier for stylized effects |
| `scale_min/max` + `scale_curve` | Size over lifetime via CurveTexture |
| `color_ramp` | Color over lifetime via GradientTexture1D |
| `emission_curve` | Emission energy over lifetime — values > 1.0 trigger HDR bloom if `use_hdr_2d` is enabled |
| `turbulence_enabled` | Noise-based displacement for organic motion |
| `anim_speed_min/max` + `anim_offset_min/max` | Flipbook playback control |
| `interp_to_end` | Force all particles to reach end of curve simultaneously — works only with ParticleProcessMaterial, must be implemented manually in custom shaders |

### When to Stay on ParticleProcessMaterial

Prefer the built-in material when the desired motion can be expressed through inspector parameters, curves, and ramps. Strong reasons to switch to a custom shader include bespoke orbital or flow-field behavior, deterministic playback state the material cannot express, or a paired custom draw shader that interprets INSTANCE_CUSTOM in a non-standard way.

---

## 3. Custom 2D Particle Shaders <a name="custom-shaders"></a>

Custom particle shaders are GPU-only. They replace ParticleProcessMaterial entirely — conveniences like `interp_to_end` must be reimplemented manually.

### 2D Transform Mapping

In a particle shader, `TRANSFORM` is a `mat4`, `VELOCITY` is a `vec3`, `CUSTOM` is a `vec4`. For 2D: treat XY as authoritative, keep Z neutral.

- `TRANSFORM[3].xy` — particle position
- `TRANSFORM[0].xy` and `TRANSFORM[1].xy` — 2D basis vectors (rotation + scale)
- `VELOCITY.xy` — 2D velocity

The basis helper below mirrors the engine's CPUParticles2D source:

```glsl
mat2 godot_basis_2d(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(vec2(c, -s), vec2(s, c));
}
```

### Practical 2D Particle Shader Template

```glsl
shader_type particles;
render_mode keep_data;

uniform vec2 emit_dir = vec2(1.0, 0.0);
uniform float initial_speed = 180.0;
uniform float spread_radians = 0.35;
uniform vec2 gravity_px = vec2(0.0, 900.0);
uniform vec2 start_scale = vec2(1.0, 1.0);
uniform float spin_radians = 0.0;
uniform float custom_frame_count = 8.0;

float rand01(inout uint s) {
    s = s * 1664525u + 1013904223u;
    return float(s & 0x00ffffffu) / float(0x01000000u);
}

mat2 godot_basis_2d(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(vec2(c, -s), vec2(s, c));
}

void start() {
    uint s = RANDOM_SEED;
    float base_angle = atan(emit_dir.y, emit_dir.x);
    float angle = base_angle + mix(-spread_radians, spread_radians, rand01(s));

    vec2 pos = TRANSFORM[3].xy;
    TRANSFORM = mat4(1.0);
    TRANSFORM[3].xy = pos;

    mat2 basis = godot_basis_2d(angle);
    TRANSFORM[0].xy = basis[0] * start_scale.x;
    TRANSFORM[1].xy = basis[1] * start_scale.y;

    VELOCITY = vec3(cos(angle), sin(angle), 0.0) * initial_speed;

    CUSTOM.x = angle;
    CUSTOM.y = 0.0;   // lifetime phase 0..1
    CUSTOM.z = floor(rand01(s) * max(custom_frame_count, 1.0));
    CUSTOM.w = 1.0;
}

void process() {
    CUSTOM.y = clamp(CUSTOM.y + DELTA / max(LIFETIME, 0.0001), 0.0, 1.0);
    VELOCITY.xy += gravity_px * DELTA;
    TRANSFORM[3].xy += VELOCITY.xy * DELTA;

    CUSTOM.x += spin_radians * DELTA;
    float scale_mul = 1.0 - CUSTOM.y;

    mat2 basis = godot_basis_2d(CUSTOM.x);
    TRANSFORM[0].xy = basis[0] * (start_scale.x * scale_mul);
    TRANSFORM[1].xy = basis[1] * (start_scale.y * scale_mul);
}
```

### INSTANCE_CUSTOM Bridging to Draw Shader

The particle shader writes to `CUSTOM`. The CanvasItem draw shader reads it as `INSTANCE_CUSTOM`:

| Channel | Convention |
|---------|-----------|
| `.x` | Rotation angle |
| `.y` | Normalized lifetime phase (0→1) |
| `.z` | Animation frame index |
| `.w` | Spare channel |

### Built-in Variables

| Variable | Type | Access | Purpose |
|----------|------|--------|---------|
| `TRANSFORM` | mat4 | read/write | Particle transform (XY = 2D position/basis) |
| `VELOCITY` | vec3 | read/write | Current velocity (XY = 2D) |
| `COLOR` | vec4 | read/write | Particle color + alpha |
| `CUSTOM` | vec4 | read/write | User data → INSTANCE_CUSTOM in draw shader |
| `LIFETIME` | float | read | Total lifetime in seconds |
| `DELTA` | float | read | Simulation time step |
| `INDEX` | uint | read | Particle index in pool |
| `RANDOM_SEED` | uint | read | Per-particle random seed |
| `ACTIVE` | bool | read/write | Set false to kill particle |
| `EMISSION_TRANSFORM` | mat4 | read | Emitter's transform |

---

## 4. Flipbook Animation <a name="flipbook"></a>

### Built-in CanvasItemMaterial Path (Preferred)

For uniform grid sprite sheets, use the built-in CanvasItemMaterial flipbook:
1. Assign a CanvasItemMaterial to the GPUParticles2D node
2. Enable particle animation
3. Set horizontal and vertical frame counts to match the atlas
4. Configure loop mode

This integrates with ParticleProcessMaterial's `anim_speed` and `anim_offset` parameters. CPUParticles2D uses animation offset as a 0→1 range from first to last frame, and animation speed as full 0→1 cycles over the particle lifetime.

### Custom CanvasItem Shader Path

When you need custom atlas control, compute tile UVs in `vertex()` — not `fragment()` — so frame selection runs once per quad vertex instead of once per pixel:

```glsl
shader_type canvas_item;

uniform vec2 atlas_frames = vec2(8.0, 8.0);
varying vec2 atlas_uv;

void vertex() {
    vec2 inv_frames = 1.0 / atlas_frames;
    float frame = INSTANCE_CUSTOM.z;

    vec2 tile = vec2(
        mod(frame, atlas_frames.x),
        floor(frame / atlas_frames.x)
    );

    atlas_uv = UV * inv_frames + tile * inv_frames;
}

void fragment() {
    COLOR = texture(TEXTURE, atlas_uv) * COLOR;
}
```

### Atlas Quality

- Pad or extrude atlas frames to prevent frame-edge bleeding
- Choose filtering intentionally: nearest for pixel art, linear+mipmaps when particles are frequently minified
- Texture repeat is disabled by default — sampling outside extents stretches edge pixels

### Avoid Runtime Bounds Polling

Do not call `GPUParticles2D.capture_rect()` every frame to resize particle bounds. It synchronizes the rendering thread and has a documented negative performance impact. Generate visibility rects offline or recompute sparingly.

---

## 5. Light2D and Normal-Mapped Particles <a name="lighting"></a>

### Normal-Mapped Particle Sprites

Use **CanvasTexture** as the particle texture. CanvasTexture is a Texture2D intended for 2D rendering that allows normal and specular maps on any CanvasItem. Its `normal_texture` property converts from standard normal-map format to the format expected by 2D rendering.

### Light Mask Partitioning

CanvasItem exposes `light_mask`, and Light2D exposes `range_item_cull_mask`. Only items with matching masks are affected. Use this as a first-class optimization tool:
- Keep expensive normal-mapped hero VFX on one light mask
- Push cheap ambience, fog, and background dust onto masks that receive little or no real lighting

### PointLight2D Tuning

- Larger lights cost more because they affect more pixels on screen
- Light `height` strongly affects normal-mapping visibility — the default height places the light so close to the surface that normal mapping may be barely visible
- Raise height to make normal maps readable on lit smoke, sparks, and magical energy

### Custom light() Function

Defining a custom `light()` function in a CanvasItem shader replaces the built-in light function entirely, even if the function is empty. The `unshaded` render mode disables lighting completely. For particle sprites with normal maps, custom draw shaders must preserve or deliberately rebuild the lighting behavior.

### Cheap Alternative: Additive Sprites

For broad ambient glow, haze, distant fire bloom, and similar background VFX, additively blended particle layers are much faster than real Light2D nodes. They do not provide normal/specular interaction but are excellent for fake glow and terrible as substitutes for a PointLight2D that sculpts normal-mapped particles.

---

## 6. Emission Shapes <a name="emission"></a>

In 2D, emission shapes are flattened interpretations of the 3D shapes:

| Shape | 2D Behavior |
|-------|-------------|
| `POINT` | All particles emit from origin |
| `SPHERE` | Sphere flattened to 2D circle |
| `SPHERE_SURFACE` | Circle perimeter |
| `RECTANGLE` | 2D rectangular area |
| `RING` | Torus flattened to ring (outer/inner radius) — added post-4.3 for CPUParticles2D |
| `POINTS` | Explicit 2D positions for authored emission masks |
| `DIRECTED_POINTS` | Explicit 2D positions + normals + colors for sprite-painted emission |

Ring emission is available in current stable CPUParticles2D source but was not listed in 4.3 docs. If targeting multiple 4.x minors, read the class reference for the exact deployed version.

---

## 7. Collision in 2D <a name="collision"></a>

### LightOccluder2D Only

GPUParticles2D collision occurs only against **LightOccluder2D** nodes, not PhysicsBody2D. There is no 2D equivalent of GPUParticlesCollisionSDF3D. The 2D renderer does produce a signed-distance field from LightOccluder2D for certain shadow tricks, but there is no ParticleProcessMaterial-level SDF collision workflow in 2D.

### Practical Implication

2D particle collision is extremely limited compared to 3D. For most 2D VFX, fake ground contact instead:
- A dark multiply or additive sprite under the effect
- A simple oval shadow Sprite2D that scales with the particle system
- Tweened `modulate` darkening on nearby surfaces
- Kill particles by lifetime or script rather than collision geometry

---

## 8. Key Properties Quick Reference <a name="properties"></a>

### GPUParticles2D

| Property | Purpose | Default Guidance |
|----------|---------|-----------------|
| `amount` | Total particle pool size | Budget against fill-rate overdraw, not just count |
| `lifetime` | Seconds each particle lives | Shorter = less overdraw accumulation |
| `one_shot` | Emit once then stop | Use for explosions, impacts |
| `explosiveness` | 0.0 = spread, 1.0 = all at once | 1.0 for bursts, 0.0 for continuous |
| `randomness` | Randomize emission timing | Adds organic variation |
| `fixed_fps` | Lock simulation tick rate | Defaults to 30; set intentionally |
| `interpolate` | Smooth between fixed ticks | Enable for smooth motion; disable for pixel-art stepped motion |
| `fract_delta` | Sub-frame accuracy | Performance trade-off at high counts |
| `visibility_rect` | Screen-space activation bounds | Set tightly — system stops rendering when rect is outside viewport |
| `draw_order` | Sorting mode | `INDEX` for consistent layering, `LIFETIME` for age-based |
| `amount_ratio` | Visible fraction | Design tool only — no performance benefit |
| `preprocess` | Pre-simulate on load | Very high values can crash the GPU |

### CPUParticles2D

| Property | Purpose | Notes |
|----------|---------|-------|
| `use_fixed_seed` | Deterministic playback | Useful for tooling and replay |
| `request_particles_process()` | Manual step | For deterministic capture workflows |
| `physics_interpolation_mode` | Physics-frame interpolation | Available in current stable, not in GPUParticles2D for 2D |
