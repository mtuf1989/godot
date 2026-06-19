# Procedural Library

Use this file when building procedural spatial materials that generate patterns at runtime without baked textures. Complements the shared foundation library at `../../foundation/procedural-noise-and-sdf-library.md` with spatial-specific patterns: tiling, warp, and material composition.

## Table of Contents

- [When to Use Procedural](#when-to-use-procedural)
- [Tiling Patterns](#tiling-patterns)
- [Warp and Distortion](#warp-and-distortion)
- [Normal Map from Procedural Height](#normal-map-from-procedural-height)
- [Material Composition Patterns](#material-composition-patterns)
- [Performance Guidelines](#performance-guidelines)

---

## When to Use Procedural

Use procedural generation when:
- Resolution independence is needed (close-up without pixelation)
- Tiling repetition must be broken (large terrain, walls)
- Animation is needed (flowing lava, pulsing energy)
- Memory budget is tight (no texture VRAM)
- Variation per instance is needed (seed-based randomization)

Use baked textures when:
- Art-directed detail is required (hand-painted surfaces)
- Performance budget is tight (procedural = ALU cost per pixel)
- The pattern is static and viewed at fixed distance

---

## Tiling Patterns

### Bricks (Running Bond)

Produces brick mask, per-brick random color, and per-brick UV for detail texturing.

```glsl
// Returns vec4(brick_mask, center.x, center.y, brick_id)
vec4 bricks(vec2 uv, vec2 count, float row_offset, float mortar, float bevel) {
    float x_offset = row_offset * step(0.5, fract(uv.y * count.y * 0.5));
    vec2 bmin = floor(vec2(uv.x * count.x - x_offset, uv.y * count.y));
    bmin.x += x_offset;
    bmin /= count;
    vec2 bmax = bmin + vec2(1.0) / count;

    vec2 size = bmax - bmin;
    float min_size = min(size.x, size.y);
    float m = mortar * min_size;
    float b = max(0.001, bevel * min_size);
    vec2 center = 0.5 * (bmin + bmax);
    vec2 d = abs(uv - center) - 0.5 * size + vec2(m);
    float dist = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
    float mask = clamp(-dist / b, 0.0, 1.0);

    vec2 tiled_pos = mod(bmin, vec2(1.0));
    float id = tiled_pos.x + 7.0 * tiled_pos.y;
    return vec4(mask, center, id);
}

// Per-brick UV (for applying detail texture per brick)
vec3 brick_uv(vec2 uv, vec2 bmin, vec2 bmax, float seed) {
    vec2 center = 0.5 * (bmin + bmax);
    vec2 size = bmax - bmin;
    float max_size = max(size.x, size.y);
    return vec3(0.5 + (uv - center) / max_size, rand(fract(center) + vec2(seed)));
}
```

### Hexagonal Tiles (Beehive)

```glsl
float hex_dist(vec2 p) {
    vec2 s = vec2(1.0, 1.73205080757);
    p = abs(p);
    return max(dot(p, s * 0.5), p.x);
}

// Returns vec4(local_offset.xy, cell_id.xy)
vec4 hex_center(vec2 p) {
    vec2 s = vec2(1.0, 1.73205080757);
    vec4 hC = floor(vec4(p, p - vec2(0.5, 1.0)) / vec4(s, s)) + 0.5;
    vec4 h = vec4(p - hC.xy * s, p - (hC.zw + 0.5) * s);
    return dot(h.xy, h.xy) < dot(h.zw, h.zw)
        ? vec4(h.xy, hC.xy)
        : vec4(h.zw, hC.zw + 9.73);
}

// Usage:
// vec2 scaled_uv = uv * vec2(columns, rows * 1.73205);
// vec4 cell = hex_center(scaled_uv);
// float mask = 1.0 - 2.0 * hex_dist(cell.xy);
// vec3 cell_color = rand3(fract(cell.zw / vec2(columns, rows)));
```

### Weave Pattern

```glsl
float weave(vec2 uv, vec2 count, float width) {
    vec2 scaled = uv * count;
    vec2 cell = floor(scaled);
    vec2 local = fract(scaled);
    float checker = mod(cell.x + cell.y, 2.0);
    // Horizontal or vertical thread based on checker
    float h_thread = smoothstep(0.5 - width, 0.5, local.y) - smoothstep(0.5, 0.5 + width, local.y);
    float v_thread = smoothstep(0.5 - width, 0.5, local.x) - smoothstep(0.5, 0.5 + width, local.x);
    return mix(h_thread, v_thread, checker);
}
```

### Truchet Pattern

```glsl
float truchet(vec2 uv, float scale, float seed) {
    vec2 scaled = uv * scale;
    vec2 cell = floor(scaled);
    vec2 local = fract(scaled);
    // Random flip per cell
    float flip = step(0.5, rand(cell + vec2(seed)));
    if (flip > 0.5) local.x = 1.0 - local.x;
    // Arc distance
    float d1 = length(local) - 0.5;
    float d2 = length(local - vec2(1.0)) - 0.5;
    return min(abs(d1), abs(d2));
}
```

---

## Warp and Distortion

### Slope Warp

Deforms an input based on the gradient (slope) of a height field. Creates organic displacement.

```glsl
// Get slope of a height function at uv
vec2 height_slope(vec2 uv, float epsilon) {
    // Replace get_height() with your procedural height function
    float h_r = get_height(fract(uv + vec2(epsilon, 0.0)));
    float h_l = get_height(fract(uv - vec2(epsilon, 0.0)));
    float h_u = get_height(fract(uv + vec2(0.0, epsilon)));
    float h_d = get_height(fract(uv - vec2(0.0, epsilon)));
    return vec2(h_r - h_l, h_u - h_d);
}

// Apply warp: sample your texture/pattern at warped UV
// vec2 warped_uv = uv + amount * height_slope(uv, epsilon);
// vec4 result = texture(my_tex, warped_uv);
```

### Distance-to-Top Warp

Like slope warp but intensity scales with distance from peaks. Creates mosaic/cracked patterns.

```glsl
vec2 distance_top_warp(vec2 uv, float epsilon, float amount) {
    vec2 slope = height_slope(uv, epsilon);
    float h = get_height(uv);
    return uv + amount * slope * (1.0 - h);
}
```

### Domain Warp (FBM-based)

Warp UV coordinates using noise for organic, non-repetitive distortion.

```glsl
// Classic domain warp: warp UV with noise, then sample pattern at warped UV
vec2 domain_warp(vec2 uv, vec2 size, float amount, float seed) {
    float nx = value_noise(uv * size, size, seed);
    float ny = value_noise(uv * size + vec2(5.2, 1.3), size, seed + 1.0);
    return uv + amount * vec2(nx - 0.5, ny - 0.5);
}

// Multi-layer domain warp (more organic)
vec2 domain_warp_fbm(vec2 uv, vec2 size, float amount, float seed) {
    vec2 q = vec2(
        value_noise(uv * size, size, seed),
        value_noise(uv * size + vec2(5.2, 1.3), size, seed + 1.0));
    vec2 r = vec2(
        value_noise((uv + amount * q) * size, size, seed + 2.0),
        value_noise((uv + amount * q) * size + vec2(1.7, 9.2), size, seed + 3.0));
    return uv + amount * r;
}
```

### Swirl

```glsl
vec2 swirl(vec2 uv, vec2 center, float angle, float radius) {
    vec2 d = uv - center;
    float dist = length(d);
    float factor = smoothstep(radius, 0.0, dist);
    float a = factor * angle;
    float ca = cos(a), sa = sin(a);
    return center + vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y);
}
```

---

## Normal Map from Procedural Height

When generating height procedurally, derive normals analytically or via finite differences.

### Finite Differences (universal, works with any height function)

```glsl
vec3 procedural_normal(vec2 uv, float eps, float strength) {
    float h_c = get_height(uv);
    float h_r = get_height(uv + vec2(eps, 0.0));
    float h_u = get_height(uv + vec2(0.0, eps));
    vec3 normal = normalize(vec3(h_c - h_r, h_c - h_u, eps / strength));
    return normal * 0.5 + 0.5; // encode for NORMAL_MAP
}
```

### For Spatial Shaders

```glsl
// In fragment():
vec3 nm = procedural_normal(UV, 1.0 / 512.0, normal_strength);
NORMAL_MAP = nm;
NORMAL_MAP_DEPTH = 1.0;
```

### Blending Two Normal Maps

```glsl
// UDN (Unreal Developer Network) blend — simple and effective
vec3 blend_normals(vec3 n1, vec3 n2) {
    n1 = n1 * 2.0 - 1.0;
    n2 = n2 * 2.0 - 1.0;
    return normalize(vec3(n1.xy + n2.xy, n1.z * n2.z)) * 0.5 + 0.5;
}

// Reoriented Normal Mapping (RNM) — more accurate
vec3 blend_normals_rnm(vec3 n1, vec3 n2) {
    n1 = n1 * 2.0 - 1.0;
    n2 = n2 * 2.0 - 1.0;
    vec3 t = n1 + vec3(0.0, 0.0, 1.0);
    vec3 u = n2 * vec3(-1.0, -1.0, 1.0);
    return normalize(t * dot(t, u) - u * t.z) * 0.5 + 0.5;
}
```

---

## Material Composition Patterns

### Height-Based Blending (Terrain Splatting)

Blend materials using height information for natural-looking transitions (dirt in crevices, snow on peaks).

```glsl
// Height-based blend between two layers
// h1, h2 = height values from each layer's heightmap
// blend = control mask (0 = layer1, 1 = layer2)
// contrast = sharpness of transition (4.0-16.0 typical)
float height_blend(float h1, float h2, float blend, float contrast) {
    float ma = max(h1 + (1.0 - blend), h2 + blend) - contrast;
    float b1 = max(h1 + (1.0 - blend) - ma, 0.0);
    float b2 = max(h2 + blend - ma, 0.0);
    return b2 / (b1 + b2);
}
```

### Procedural Weathering

Add wear to edges using curvature approximation.

```glsl
// Approximate curvature from normal map (edge wear)
float curvature_from_normal(sampler2D normal_tex, vec2 uv, float texel_size) {
    vec3 n_c = texture(normal_tex, uv).rgb * 2.0 - 1.0;
    vec3 n_r = texture(normal_tex, uv + vec2(texel_size, 0.0)).rgb * 2.0 - 1.0;
    vec3 n_u = texture(normal_tex, uv + vec2(0.0, texel_size)).rgb * 2.0 - 1.0;
    return (length(n_c - n_r) + length(n_c - n_u)) * 0.5;
}
```

### Triplanar Procedural

Apply procedural patterns in world space without UV seams.

```glsl
float triplanar_procedural(vec3 world_pos, vec3 world_normal, float scale) {
    vec3 blend = pow(abs(world_normal), vec3(4.0));
    blend /= dot(blend, vec3(1.0));
    // Sample your procedural function on each plane
    float x_val = get_pattern(world_pos.yz * scale);
    float y_val = get_pattern(world_pos.xz * scale);
    float z_val = get_pattern(world_pos.xy * scale);
    return x_val * blend.x + y_val * blend.y + z_val * blend.z;
}
```

### Seed Variation (Per-Instance Procedural)

Use `instance uniform` to give each mesh instance unique procedural variation.

```glsl
instance uniform float variation_seed : hint_range(0.0, 100.0) = 0.0;

void fragment() {
    // Offset noise seed per instance
    float height = fbm(UV, vec2(4.0), 5, 0.5, 0, variation_seed);
    // ...
}
```

---

## Performance Guidelines

| Technique | Cost per Pixel | Notes |
|---|---|---|
| Single noise sample | Low | ~10 ALU ops |
| FBM 4 octaves | Medium | ~40 ALU ops |
| FBM 8 octaves | High | ~80 ALU ops |
| Voronoi (3×3 search) | Medium | 9 iterations + distance |
| Triplanar procedural | 3× base cost | Three evaluations |
| Domain warp + FBM | High | Nested noise evaluations |
| Raymarching (100 steps) | Very High | Full SDF per step |

### Optimization Strategies

1. **LOD by distance**: Reduce octaves or switch to texture at distance
   ```glsl
   float dist = length(VERTEX);
   int octaves = int(mix(6.0, 2.0, smoothstep(5.0, 30.0, dist)));
   ```

2. **Bake to texture at runtime**: Use `SubViewport` to render procedural to texture once, then sample normally

3. **Precompute in vertex shader**: Pass coarse noise via `varying` for per-vertex evaluation, refine in fragment only where needed

4. **Avoid dynamic loops**: Godot's shader compiler handles fixed iteration counts better. Use `const int` for loop bounds when possible.

5. **Texture-assisted procedural**: Use a small tileable noise texture for the base octave, add 1-2 procedural octaves on top for variation. Best of both worlds.

---

## Cross-References

- Shared noise/SDF/blend implementations: `../../foundation/procedural-noise-and-sdf-library.md`
- Material patterns using these building blocks: `material-catalog.md`
- Raymarching (SDF rendering): `advanced-techniques.md#raymarching`
- Integration and validation: `integration-checklist.md`
