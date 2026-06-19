# Terrain Generation

Reference for GPU-driven terrain heightmap synthesis, erosion simulation, and real-time modification in Godot 4 compute shaders.

## Table of Contents

1. [Architecture Overview](#architecture)
2. [Heightmap Synthesis](#heightmap)
3. [Hydraulic Erosion](#hydraulic)
4. [Thermal Erosion](#thermal)
5. [Real-Time Modification](#realtime)
6. [Rendering Bridge](#rendering)
7. [Resource Layout](#resources)

---

## 1. Architecture Overview <a name="architecture"></a>

A GPU-resident heightfield pipeline on the **main RenderingDevice**:

- Compute writes directly into persistent float textures
- Textures exposed to the terrain material through `Texture2DRD`
- CPU interaction is exceptional, not per-frame
- Ping-pong (front/back) textures for passes that read and write the same field

This follows Godot's official Compute Texture Demo pattern: create textures on the RD, run compute on the main device, expose results via `Texture2DRD`.

### Workgroup Layout

Terrain passes are 2D — one thread per texel:

```glsl
layout(local_size_x = 8, local_size_y = 8) in;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= terrain_width || texel.y >= terrain_height) return;
    // ...
}
```

Dispatch: `ceil(width / 8) × ceil(height / 8) × 1`.

## 2. Heightmap Synthesis <a name="heightmap"></a>

### Layered Noise with Domain Warping

```
H(p) = Σ_i  a_i * N(λ_i * (p + ω * W(p)))
```

Where:
- `N` is improved Perlin or simplex noise
- `W(p)` is a low-frequency 2D warp field (breaks axis-aligned repetition)
- `a_i` follows gain/persistence, `λ_i` follows lacunarity

### Conceptual Bands

Separate the generator into three bands:
1. **Continental** — macro elevation, very low frequency
2. **Ridge/warp** — mountain layout, domain warping
3. **Detail** — secondary breakup, higher frequency

This gives erosion meaningful large-scale structure to work with.

### Seam-Safe Streaming

For large worlds with tiled terrain, seed each tile from **world-space origin**, not local UV. Adjacent tiles share the same absolute noise domain and remain seam-safe.

### Implementation

One compute dispatch, one invocation per texel, writing to `height_front`:

```glsl
layout(rgba32f, binding = 0) uniform image2D height_out;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    vec2 world_pos = domain_min + vec2(texel) * texel_size;

    // Domain warp
    vec2 warp = vec2(noise(world_pos * 0.001), noise(world_pos * 0.001 + 100.0));
    vec2 warped = world_pos + warp * warp_strength;

    // Octave sum
    float h = 0.0;
    float amp = 1.0;
    float freq = base_frequency;
    for (int i = 0; i < octaves; i++) {
        h += amp * noise(warped * freq);
        amp *= persistence;
        freq *= lacunarity;
    }

    imageStore(height_out, texel, vec4(h, 0.0, 0.0, 0.0));
}
```

## 3. Hydraulic Erosion <a name="hydraulic"></a>

Eulerian grid solver (shallow-water / virtual-pipe model). Per cell: terrain height `b`, water depth `d`, suspended sediment `s`, outflow flux `f`, velocity `v`.

### Pass Sequence

Six passes per erosion iteration, each a separate compute dispatch with barriers between dependent passes:

**Pass 1 — Water Source:**
Add rainfall or brush-authored water injection into `water_front`.

**Pass 2 — Flux:**
Compute pressure-driven outflow to 4 cardinal neighbors. Clamp total outflow so it cannot exceed current water:

```glsl
float total_h = b + d;
float flux_L = max(0.0, total_h - neighbor_L_total_h) * pipe_area * gravity / cell_size;
// ... repeat for R, U, D
float scale = min(1.0, d * cell_area / (dt * (flux_L + flux_R + flux_U + flux_D + 1e-6)));
flux_L *= scale; // ... etc
```

**Pass 3 — Velocity:**
Reconstruct 2D velocity from neighboring flux values.

**Pass 4 — Erosion/Deposition:**
Compute transport capacity `C` from slope and speed. If sediment < C, erode terrain into sediment. If sediment > C, deposit back. Adjust water during erosion/deposition to suppress ripple instabilities.

**Pass 5 — Sediment Advection:**
Move suspended sediment along velocity field using backtracing with bilinear interpolation.

**Pass 6 — Evaporation:**
Reduce residual water by evaporation factor.

### Ping-Pong

Never read and write the same erosion field in-place during a neighborhood pass. Use `front/back` textures per evolving variable, swap RIDs after the pass group.

## 4. Thermal Erosion <a name="thermal"></a>

Separate pass family from hydraulic. Uses 8 virtual pipes and a talus-angle criterion:

For each texel:
1. Read center height and 8 neighbors
2. Compute slope to each lower neighbor
3. If slope exceeds talus angle, compute capped transport amount
4. Distribute material proportionally to eligible lower neighbors

Run thermal erosion **after** hydraulic in the same frame for realistic gully sharpening. Decimate temporally for gameplay edits: every 2nd–3rd frame, or only after the user stops digging.

## 5. Real-Time Modification <a name="realtime"></a>

### Command-Driven Brush System

Keep a storage buffer of brush commands:

| Field | Type | Purpose |
|-------|------|---------|
| `center` | vec2 | World-space position |
| `radius` | float | Brush radius |
| `falloff` | float | Edge softness |
| `amplitude` | float | Signed height delta |
| `type` | uint | Crater, raise/lower, smooth, trench |
| `rotation` | float | For directional stamps |

A compute pass converts commands into height deltas over affected texels, then composes them into the persistent height texture.

### Crater Brush

Signed radial profile with three zones:
1. Central depression (negative delta)
2. Raised rim (positive delta)
3. Optional outer ejecta noise

### Trench Brush

Swept signed-distance field along a polyline/spline. Width varies per control point. Sidewall profile can be sharpened by thermal erosion after the stamp.

### Dirty-Tile Optimization

Dispatch only over affected regions:
1. Brush pass rasterizes affected texel bounds into a tile mask buffer
2. Subsequent erosion/normal/splat passes dispatch per dirty tile or use indirect dispatch
3. Terrain editing is usually sparse in space even when frequent in time

## 6. Rendering Bridge <a name="rendering"></a>

### Texture2DRD to Terrain Material

```gdscript
var height_tex := Texture2DRD.new()
height_tex.texture_rd_rid = height_front_rid

terrain_material.set_shader_parameter("heightmap", height_tex)
```

The spatial shader samples the heightmap for vertex displacement:

```glsl
uniform sampler2D heightmap;

void vertex() {
    float h = texture(heightmap, UV).r;
    VERTEX.y += h * height_scale;
}
```

### Normal Map Generation

Derive normals from the heightmap in a separate compute pass after erosion:

```glsl
float hL = imageLoad(height, texel + ivec2(-1, 0)).r;
float hR = imageLoad(height, texel + ivec2( 1, 0)).r;
float hD = imageLoad(height, texel + ivec2( 0,-1)).r;
float hU = imageLoad(height, texel + ivec2( 0, 1)).r;

vec3 normal = normalize(vec3(hL - hR, 2.0 * texel_size, hD - hU));
imageStore(normal_out, texel, vec4(normal * 0.5 + 0.5, 1.0));
```

### Splat Map Generation

Derive material weights from slope, height, erosion state:

```glsl
float slope = 1.0 - normal.y;  // 0 = flat, 1 = vertical
float rock = smoothstep(0.4, 0.7, slope);
float grass = 1.0 - rock;
imageStore(splat_out, texel, vec4(grass, rock, 0.0, 0.0));
```

## 7. Resource Layout <a name="resources"></a>

| Resource | Format | Purpose |
|----------|--------|---------|
| `height_front` / `height_back` | R32_SFLOAT | Displacement, ping-pong |
| `water_front` / `water_back` | R32_SFLOAT | Water depth |
| `sediment_front` / `sediment_back` | R32_SFLOAT | Suspended sediment |
| `velocity` | RG32_SFLOAT | 2D flow velocity |
| `flux` | RGBA32_SFLOAT | 4-directional outflow |
| `hardness` | R32_SFLOAT | Optional per-texel material hardness |
| `normal_map` | RGBA8_UNORM | Derived normals for rendering |
| `splat_map` | RGBA8_UNORM | Derived material weights |
| `brush_commands` | SSBO | CPU-written brush command buffer |

All textures created with `TEXTURE_USAGE_STORAGE_BIT | TEXTURE_USAGE_SAMPLING_BIT`. Height/water/sediment exposed to materials via `Texture2DRD`.
