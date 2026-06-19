# Particle Systems Reference

Comprehensive reference for GPUParticles3D, ParticleProcessMaterial, custom particle shaders, sub-emitters, trails, collision, and attractors in Godot 4.

## Table of Contents

1. [GPUParticles3D vs CPUParticles3D](#gpu-vs-cpu)
2. [ParticleProcessMaterial Configuration](#process-material)
3. [Custom Particle Shaders](#custom-particle-shaders)
4. [Sub-Emitters](#sub-emitters)
5. [Trail Rendering](#trail-rendering)
6. [Collision](#collision)
7. [Attractors and Vector Fields](#attractors)
8. [Emission Shapes and Mesh Surfaces](#emission)
9. [Draw Passes and Mesh Selection](#draw-passes)

---

## 1. GPUParticles3D vs CPUParticles3D <a name="gpu-vs-cpu"></a>

### When to Use GPU

GPUParticles3D offloads the entire particle lifecycle to the graphics hardware via compute shaders or transform feedback. Simulation data stays resident in VRAM — no PCIe bus transfer bottleneck. This is the only path for:
- particle counts above ~50 concurrent elements
- sub-emitters
- GPU collision (SDF, heightfield, analytical shapes)
- GPU attractors and vector fields
- trail meshes (RibbonTrailMesh, TubeTrailMesh)
- custom particle process shaders

As of the Godot 4.2 refactor, CPUParticles is officially a lower-end fallback. New features are developed for GPUParticles only. Use the built-in conversion tool to migrate legacy CPU particle materials.

### When CPU Is Acceptable

CPUParticles3D has lower dispatch overhead for very small emissions (under ~10 particles). A single spark or a brief 5-particle puff incurs less base cost on the CPU than dispatching a GPU compute workgroup where 123 of 128 threads idle. Use CPUParticles3D only for:
- micro-emissions where GPU dispatch overhead exceeds the simulation cost
- explicit hardware fallback requirements (e.g., targeting devices without compute shader support)

### Key GPUParticles3D Properties

| Property | Purpose | Default Guidance |
|----------|---------|-----------------|
| `amount` | Total particle pool size | Budget against overdraw, not just count |
| `lifetime` | Seconds each particle lives | Shorter = less overdraw accumulation |
| `one_shot` | Emit once then stop | Use for explosions, impacts |
| `explosiveness` | 0.0 = spread over lifetime, 1.0 = all at once | 1.0 for bursts, 0.0 for continuous |
| `randomness` | Randomize emission timing | Adds organic variation to continuous emitters |
| `fixed_fps` | Lock simulation to fixed tick rate | Set to 30 or 60 and enable `interpolate` |
| `interpolate` | Smooth between fixed ticks | Always enable when `fixed_fps` is set |
| `fract_delta` | Use fractional delta for sub-frame accuracy | Enable for smooth emission at low fixed_fps |
| `visibility_aabb` | Bounding box for culling | Set tightly — particles outside this box are culled |
| `draw_order` | Sorting mode for transparency | `VIEW_DEPTH` for correct blending, `LIFETIME` for consistent layering |
| `transform_align` | Billboard mode | `DISABLED` for 3D meshes, `Y_TO_VELOCITY` for directional sprites |

### Fixed FPS and Interpolation

Particle physics run at a fixed update rate. If the monitor refresh rate differs from the simulation tick, particles stutter visibly. Always:
1. Set `fixed_fps` to 30 or 60
2. Enable `interpolate = true`
3. Enable `fract_delta = true`

This ensures smooth visual motion regardless of frame time variance.

---

## 2. ParticleProcessMaterial Configuration <a name="process-material"></a>

ParticleProcessMaterial is the built-in material that drives particle simulation without writing shader code. It covers the vast majority of VFX needs.

### Velocity and Motion

- `direction` + `spread`: initial emission cone
- `initial_velocity_min/max`: speed at birth
- `velocity_limit`: hard speed cap (new in 4.2)
- `gravity`: constant acceleration vector
- `damping` + `damping_as_friction`: deceleration model
  - Standard damping: constant negative acceleration (physically accurate)
  - Friction damping: velocity-proportional drag (snappier, better for stylized effects)

### Scale and Shape

- `scale_min/max`: initial size range
- `scale_over_velocity`: size responds to speed (useful for stretch effects)
- `scale_curve`: size over normalized lifetime (CurveTexture)
- `interp_to_end`: force all particles to reach end of curve simultaneously

### Color and Emission

- `color`: base tint
- `color_ramp`: color over normalized lifetime (GradientTexture1D)
- `color_initial_ramp`: color variation at birth
- `emission_curve`: emission energy over lifetime
  - Values > 1.0 trigger HDR bloom
  - Separate from alpha: a particle can glow intensely while fading out
- `hue_variation_min/max`: per-particle hue randomization

### Turbulence

- `turbulence_enabled`: activate noise-based displacement
- `turbulence_noise_strength/scale/speed`: control turbulence character
- `turbulence_influence_min/max`: how much turbulence affects velocity

### Animation

- `anim_speed_min/max`: flipbook playback speed
- `anim_offset_min/max`: randomize starting frame per particle
- Used with `ParticleProcessMaterial.PARAM_ANIM_SPEED` and mesh UV configuration

---

## 3. Custom Particle Shaders <a name="custom-particle-shaders"></a>

When ParticleProcessMaterial is insufficient, write a custom `shader_type particles` shader.

### When to Use Custom Shaders

- Complex emission logic (emit from SDF surface, emit along spline)
- CUSTOM data packing for INSTANCE_CUSTOM bridging to spatial shaders
- Non-standard physics (orbital motion, magnetic fields, custom drag models)
- Procedural color or scale logic that exceeds curve textures
- Secondary emission via `emit_subparticle()`

### Shader Structure

```glsl
shader_type particles;

uniform float speed : hint_range(0.0, 50.0) = 10.0;
uniform sampler2D noise_texture;

void start() {
    // Called once when particle spawns
    // Initialize TRANSFORM, VELOCITY, COLOR, CUSTOM
    VELOCITY = vec3(0.0, speed, 0.0);
    CUSTOM.x = float(INDEX); // birth seed
    CUSTOM.y = 0.0;          // normalized age (updated in process)
}

void process() {
    // Called every simulation tick
    CUSTOM.y = 1.0 - (LIFETIME - ELAPSED_TIME) / LIFETIME; // normalized age 0→1

    // Sample noise for turbulence
    vec2 noise_uv = TRANSFORM[3].xz * 0.1 + TIME * 0.05;
    float noise_val = texture(noise_texture, noise_uv).r;
    VELOCITY.x += (noise_val - 0.5) * 2.0;

    // Apply gravity
    VELOCITY.y -= 9.8 * DELTA;

    // Update position
    TRANSFORM[3].xyz += VELOCITY * DELTA;
}
```

### Built-in Variables

| Variable | Type | Access | Purpose |
|----------|------|--------|---------|
| `TRANSFORM` | mat4 | read/write | Particle world transform |
| `VELOCITY` | vec3 | read/write | Current velocity |
| `COLOR` | vec4 | read/write | Particle color + alpha |
| `CUSTOM` | vec4 | read/write | User data → becomes INSTANCE_CUSTOM in mesh shader |
| `LIFETIME` | float | read | Total lifetime in seconds |
| `ELAPSED_TIME` | float | read | Time since birth |
| `DELTA` | float | read | Simulation time step |
| `INDEX` | uint | read | Particle index in pool |
| `NUMBER` | uint | read | Sequential emission number |
| `EMISSION_TRANSFORM` | mat4 | read | Emitter's transform |
| `ACTIVE` | bool | read/write | Set false to kill particle |
| `RESTART_POSITION` | bool | read | True on first frame |
| `RESTART_VELOCITY` | bool | read | True on first frame |
| `USERDATAX` | vec4 | read/write | Up to 6 extra data channels (USERDATA1–USERDATA6) |

### emit_subparticle()

Spawn secondary particles from within a custom particle shader:

```glsl
void process() {
    // Emit a spark sub-particle at 80% lifetime
    if (CUSTOM.y > 0.8 && CUSTOM.z < 0.5) {
        CUSTOM.z = 1.0; // flag: already emitted
        emit_subparticle(
            TRANSFORM,
            VELOCITY * 0.5,
            vec4(1.0, 0.5, 0.0, 1.0), // color
            vec4(0.0),                  // custom
            FLAG_EMIT_POSITION | FLAG_EMIT_ROT_SCALE | FLAG_EMIT_VELOCITY | FLAG_EMIT_COLOR
        );
    }
}
```

Flags control which properties the sub-particle inherits from the parent.

---

## 4. Sub-Emitters <a name="sub-emitters"></a>

Sub-emitters allow a primary GPUParticles3D to spawn secondary particle systems at lifecycle events. All logic runs on the GPU.

### Trigger Modes

| Mode | When It Fires | Use Case |
|------|---------------|----------|
| `AT_END` | When parent particle dies | Smoke after flash, embers after explosion |
| `AT_COLLISION` | When parent hits collision geometry | Sparks on impact, splash on water |
| `CONSTANT` | Every simulation tick while parent lives | Continuous trail, smoke from moving projectile |

### Setup

1. Create the secondary GPUParticles3D node as a child or sibling
2. On the primary node's ParticleProcessMaterial, set `sub_emitter` to the secondary node's path
3. Set `sub_emitter_mode` to the desired trigger
4. Configure `sub_emitter_frequency` for CONSTANT mode (how often to emit)
5. Configure `sub_emitter_amount_at_end` or `sub_emitter_amount_at_collision` for burst counts
6. Set `sub_emitter_keep_velocity` to inherit parent velocity

### Chaining Multi-Stage Effects

A typical explosion pipeline:
1. **Primary burst** — `one_shot = true`, `explosiveness = 1.0`, short lifetime (0.1–0.3s), bright emissive flash
2. **Secondary smoke** — sub-emitter of primary, mode `AT_END`, longer lifetime (1–3s), dark albedo, turbulence
3. **Tertiary sparks** — sub-emitter of primary, mode `AT_COLLISION`, high initial velocity, gravity, short lifetime

Godot prohibits recursive sub-emitters (a system cannot spawn itself). This enforces acyclic effect graphs.

### When to Use Script Triggering Instead

Sub-emitters are ideal when the secondary effect should spawn at the exact position and time of the parent particle's event. Use GDScript triggering when:
- the secondary effect needs to spawn at a fixed world position (not per-particle)
- the timing depends on gameplay logic rather than particle lifecycle
- the secondary system needs different collision or attractor configuration

---

## 5. Trail Rendering <a name="trail-rendering"></a>

### RibbonTrailMesh

Flat ribbon geometry skinned along particle trajectory history. Ideal for:
- weapon swooshes
- magical ribbons
- light streaks
- stylized projectile trails

UV maps along the trail length, allowing scrolling textures that conform to the path.

### TubeTrailMesh

Cylindrical geometry along trajectory. Ideal for:
- energy beams
- thick smoke trails
- organic tendrils

### Configuration

- Set `trail_enabled = true` on GPUParticles3D
- Set `trail_lifetime` (how long the trail persists behind each particle)
- Assign RibbonTrailMesh or TubeTrailMesh as the draw pass mesh
- Configure `section_length` and `sections` for geometry density
- Apply a spatial ShaderMaterial for scrolling, fading, or distortion along the trail UV

---

## 6. Collision <a name="collision"></a>

GPU particles cannot query CPU physics colliders. Collision is handled by dedicated GPU collision nodes.

### Collision Node Types

| Node | Shape | Use Case |
|------|-------|----------|
| `GPUParticlesCollisionSphere3D` | Sphere | Simple radial boundary |
| `GPUParticlesCollisionBox3D` | Box | Room boundaries, flat surfaces |
| `GPUParticlesCollisionHeightField3D` | Heightmap | Large outdoor terrain |
| `GPUParticlesCollisionSDF3D` | Signed Distance Field | Complex indoor environments, caves, overhangs |

### SDF Collision

GPUParticlesCollisionSDF3D bakes a 3D texture where each voxel stores the distance to the nearest surface. The particle shader queries this texture at the particle's position:
- distance ≤ particle radius → collision detected
- gradient of the SDF at that point → collision normal for bounce/slide

SDF collision handles complex topology (tunnels, caves, overhangs) entirely on the GPU. Bake resolution trades accuracy for VRAM. Rebake when static geometry changes.

### Collision Response

Configure on ParticleProcessMaterial:
- `collision_mode`: `RIGID` (bounce) or `HIDE_ON_CONTACT` (destroy)
- `collision_bounce`: restitution coefficient (0 = no bounce, 1 = perfect bounce)
- `collision_friction`: surface friction on sliding
- `collision_use_scale`: scale affects collision radius

---

## 7. Attractors and Vector Fields <a name="attractors"></a>

### Attractor Nodes

| Node | Behavior |
|------|----------|
| `GPUParticlesAttractorSphere3D` | Radial pull/push toward center |
| `GPUParticlesAttractorBox3D` | Directional force within box volume |
| `GPUParticlesAttractorVectorField3D` | 3D texture-driven flow field |

### Vector Field Attractors

GPUParticlesAttractorVectorField3D uses a Texture3D where RGB channels encode directional velocity vectors. Particles sample the texture at their local position and inherit the encoded acceleration.

Use cases:
- swirling vortex effects
- localized wind currents
- magical black-hole pull
- channeled energy flow

Import requirements for vector field Texture3D:
- **No lossy compression** — S3TC/BPTC quantizes RGB channels, causing particles to jitter or diverge
- Use 16-bit or 32-bit float per channel for physical data fidelity
- Match coordinate system: Godot is Y-up, apply axis conversion if source data is Z-up

### Attractor Properties

- `strength`: force magnitude (negative = repel)
- `directionality`: 0 = toward center, 1 = along attractor's forward axis
- `attenuation`: falloff curve from center to edge

---

## 8. Emission Shapes and Mesh Surfaces <a name="emission"></a>

ParticleProcessMaterial supports multiple emission shapes:

| Shape | Description |
|-------|-------------|
| `POINT` | All particles emit from origin |
| `SPHERE` / `SPHERE_SURFACE` | Volume or surface of sphere |
| `BOX` | Volume of axis-aligned box |
| `RING` | Torus shape with configurable radii |
| `MESH` | Emit from vertices/faces of a mesh resource |

Mesh emission is powerful for effects that conform to character or object geometry (aura, burning, disintegration). The emission mesh should be a simplified proxy, not the full-detail game mesh.

---

## 9. Draw Passes and Mesh Selection <a name="draw-passes"></a>

GPUParticles3D supports up to 4 draw passes, each rendering the same particle data with a different mesh and material. Use cases:
- Pass 1: core glow (small, bright, additive)
- Pass 2: smoke body (larger, dark, alpha blended)
- Pass 3: distortion plane (screen-reading shader)

Each pass adds a draw call. Use multiple passes only when the visual benefit justifies the cost.

### Mesh Selection Guidelines

| Mesh Type | When to Use |
|-----------|-------------|
| `QuadMesh` | Default billboard sprite |
| Custom trimmed mesh | When >20% of quad area is transparent — trim to fit opaque bounds |
| `SphereMesh` / `BoxMesh` | 3D debris, Yutapon cubes |
| `RibbonTrailMesh` | Flat trails |
| `TubeTrailMesh` | Volumetric trails |
| Custom 3D mesh | Specific debris shapes, projectile geometry |

For billboard alignment, set `transform_align` on GPUParticles3D:
- `DISABLED`: no alignment (3D meshes)
- `Z_BILLBOARD`: face camera (standard sprites)
- `Y_TO_VELOCITY`: orient along movement direction (sparks, streaks)
- `Z_BILLBOARD_Y_TO_VELOCITY`: face camera but stretch along velocity
