# Procedural Noise & SDF Function Library

Production-tested GLSL building blocks for procedural shader materials. Extracted from Material Maker's node library and adapted for direct use in Godot 4 `.gdshader` files.

Both `godot-shader-spatial` and `godot-shader-canvasitem-fx` reference this file for copy-paste-ready implementations.

## Table of Contents

- [Hash Functions](#hash-functions)
- [Noise Functions](#noise-functions)
- [FBM (Fractal Brownian Motion)](#fbm-fractal-brownian-motion)
- [SDF 2D Primitives](#sdf-2d-primitives)
- [SDF 3D Primitives](#sdf-3d-primitives)
- [SDF Operations](#sdf-operations)
- [Blend Modes](#blend-modes)
- [Color Utilities](#color-utilities)

---

## Hash Functions

All noise functions below depend on these deterministic hash functions. They produce pseudo-random values from spatial coordinates, enabling tileable and seedable patterns.

```glsl
float rand(vec2 x) {
    return fract(cos(mod(dot(x, vec2(13.9898, 8.141)), 3.14)) * 43758.5);
}

vec2 rand2(vec2 x) {
    return fract(cos(mod(vec2(dot(x, vec2(13.9898, 8.141)),
                              dot(x, vec2(3.4562, 17.398))), vec2(3.14))) * 43758.5);
}

vec3 rand3(vec2 x) {
    return fract(cos(mod(vec3(dot(x, vec2(13.9898, 8.141)),
                              dot(x, vec2(3.4562, 17.398)),
                              dot(x, vec2(13.254, 5.867))), vec3(3.14))) * 43758.5);
}

// 3D input hash (for 3D noise / triplanar)
float rand31(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

vec3 rand33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}
```

---

## Noise Functions

### Value Noise (2D, tileable)

Cheapest noise. Interpolates random values at grid corners. Good for displacement, roughness variation.

```glsl
float value_noise(vec2 coord, vec2 size, float seed) {
    vec2 o = floor(coord) + rand2(vec2(seed, 1.0 - seed)) + size;
    vec2 f = fract(coord);
    float p00 = rand(mod(o, size));
    float p01 = rand(mod(o + vec2(0.0, 1.0), size));
    float p10 = rand(mod(o + vec2(1.0, 0.0), size));
    float p11 = rand(mod(o + vec2(1.0, 1.0), size));
    vec2 t = f * f * (3.0 - 2.0 * f);
    return mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);
}
```

### Perlin Noise (2D, tileable)

Gradient noise. Smoother than value noise, zero-centered. Good for organic terrain, clouds.

```glsl
float perlin_noise(vec2 coord, vec2 size, float seed) {
    vec2 o = floor(coord) + rand2(vec2(seed, 1.0 - seed)) + size;
    vec2 f = fract(coord);
    float a00 = rand(mod(o, size)) * 6.28318530718;
    float a01 = rand(mod(o + vec2(0.0, 1.0), size)) * 6.28318530718;
    float a10 = rand(mod(o + vec2(1.0, 0.0), size)) * 6.28318530718;
    float a11 = rand(mod(o + vec2(1.0, 1.0), size)) * 6.28318530718;
    vec2 v00 = vec2(cos(a00), sin(a00));
    vec2 v01 = vec2(cos(a01), sin(a01));
    vec2 v10 = vec2(cos(a10), sin(a10));
    vec2 v11 = vec2(cos(a11), sin(a11));
    float p00 = dot(v00, f);
    float p01 = dot(v01, f - vec2(0.0, 1.0));
    float p10 = dot(v10, f - vec2(1.0, 0.0));
    float p11 = dot(v11, f - vec2(1.0, 1.0));
    vec2 t = f * f * (3.0 - 2.0 * f);
    return 0.5 + mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);
}
```

### Cellular / Voronoi Noise (2D, tileable)

Returns `vec4(cell_center.xy, distance_to_center, distance_to_edge)`. Multiple distance metrics available.

```glsl
// Based on Inigo Quilez - https://www.shadertoy.com/view/ldl3W8
vec4 voronoi(vec2 uv, vec2 size, float intensity, float randomness, float seed) {
    uv *= size;
    vec2 seed2 = rand2(vec2(seed, 1.0 - seed));
    vec2 n = floor(uv);
    vec2 f = fract(uv);
    vec2 mg, mr, mc;
    float md = 8.0;
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
        vec2 g = vec2(float(i), float(j));
        vec2 o = randomness * rand2(seed2 + mod(n + g + size, size));
        vec2 c = g + o;
        vec2 r = c - f;
        float d = dot(r, r);
        if (d < md) { mc = c; md = d; mr = r; mg = g; }
    }
    md = 8.0;
    for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++) {
        vec2 g = mg + vec2(float(i), float(j));
        vec2 o = randomness * rand2(seed2 + mod(n + g + size, size));
        vec2 r = g + o - f;
        if (dot(mr - r, mr - r) > 0.00001)
            md = min(md, dot(0.5 * (mr + r), normalize(r - mr)));
    }
    return vec4(mc + n, intensity * length(mr), md);
}
```

#### Cellular Distance Variants

```glsl
// Euclidean F1 (distance to nearest cell center)
float cellular_euclidean(vec2 coord, vec2 size, float seed) {
    vec2 o = floor(coord) + rand2(vec2(seed, 1.0 - seed)) + size;
    vec2 f = fract(coord);
    float min_dist = 2.0;
    for (float x = -1.0; x <= 1.0; x++)
    for (float y = -1.0; y <= 1.0; y++) {
        vec2 node = rand2(mod(o + vec2(x, y), size)) + vec2(x, y);
        min_dist = min(min_dist, length(f - node));
    }
    return min_dist;
}

// Manhattan F1
float cellular_manhattan(vec2 coord, vec2 size, float seed) {
    vec2 o = floor(coord) + rand2(vec2(seed, 1.0 - seed)) + size;
    vec2 f = fract(coord);
    float min_dist = 2.0;
    for (float x = -1.0; x <= 1.0; x++)
    for (float y = -1.0; y <= 1.0; y++) {
        vec2 node = rand2(mod(o + vec2(x, y), size)) * 0.5 + vec2(x, y);
        min_dist = min(min_dist, abs(f.x - node.x) + abs(f.y - node.y));
    }
    return min_dist;
}

// Chebyshev F1
float cellular_chebyshev(vec2 coord, vec2 size, float seed) {
    vec2 o = floor(coord) + rand2(vec2(seed, 1.0 - seed)) + size;
    vec2 f = fract(coord);
    float min_dist = 2.0;
    for (float x = -1.0; x <= 1.0; x++)
    for (float y = -1.0; y <= 1.0; y++) {
        vec2 node = rand2(mod(o + vec2(x, y), size)) + vec2(x, y);
        min_dist = min(min_dist, max(abs(f.x - node.x), abs(f.y - node.y)));
    }
    return min_dist;
}

// F2-F1 (cracks/veins pattern) — works with any distance metric
float cellular_f2f1(vec2 coord, vec2 size, float seed) {
    vec2 o = floor(coord) + rand2(vec2(seed, 1.0 - seed)) + size;
    vec2 f = fract(coord);
    float d1 = 2.0, d2 = 2.0;
    for (float x = -1.0; x <= 1.0; x++)
    for (float y = -1.0; y <= 1.0; y++) {
        vec2 node = rand2(mod(o + vec2(x, y), size)) + vec2(x, y);
        float d = length(f - node);
        if (d < d1) { d2 = d1; d1 = d; }
        else if (d < d2) { d2 = d; }
    }
    return d2 - d1;
}
```

### Wavelet Noise (2D, tileable)

Rotation-based noise with less repetition than value/Perlin. Good for non-repetitive organic patterns.

```glsl
float wavelet(vec2 uv, vec2 size, float s, float frequency, float offset) {
    uv = mod(uv, size);
    vec2 seed = fract(floor(uv) * 0.1236754 + vec2(s));
    uv = fract(uv);
    vec2 ruv = uv - 0.5;
    float a = rand(seed) * 6.28;
    float ca = cos(a), sa = sin(a);
    ruv = vec2(ca * ruv.x + sa * ruv.y, -sa * ruv.x + ca * ruv.y);
    return (0.5 * sin(ruv.x * 6.28 * frequency + offset) + 0.5)
           * max(0.0, 1.0 - 2.0 * length(uv - vec2(0.5)));
}

float wavelet_noise(vec2 uv, vec2 size, int iterations, float persistence, float seed, float frequency, float offset) {
    float rv = 0.0, acc = 0.0, q = 1.0;
    vec2 seed2 = rand2(vec2(seed));
    vec2 local_uv = uv * size;
    for (int i = 0; i < iterations; ++i) {
        rv += q * wavelet(local_uv, size, seed, frequency, offset);
        rv += q * wavelet(local_uv + vec2(0.5), size, seed + 0.1, frequency, offset);
        acc += q;
        local_uv += uv; size += vec2(1.0);
        local_uv += seed2;
        seed2 = rand2(seed2);
        q *= persistence;
        seed += 0.1;
    }
    return rv / acc;
}
```

### Anisotropic Noise (2D, tileable)

Directional noise — useful for brushed metal, wood grain, hair flow maps.

```glsl
float anisotropic(vec2 uv, vec2 size, float seed, float smoothness, float interpolation) {
    vec2 seed2 = rand2(vec2(seed, 1.0 - seed));
    vec2 xy = floor(uv * size);
    vec2 offset = vec2(rand(seed2 + xy.y), 0.0);
    vec2 xy_offset = floor(uv * size + offset);
    float f0 = rand(seed2 + mod(xy_offset, size));
    float f1 = rand(seed2 + mod(xy_offset + vec2(1.0, 0.0), size));
    float mixer = clamp((fract(uv.x * size.x + offset.x) - 0.5) / smoothness + 0.5, 0.0, 1.0);
    float smooth_mix = smoothstep(0.0, 1.0, mixer);
    return mix(mix(f0, f1, mixer), mix(f0, f1, smooth_mix), interpolation);
}
```

---

## FBM (Fractal Brownian Motion)

Layer any noise function with octaves for natural-looking complexity. The `folds` parameter applies `abs(2*noise-1)` for ridged/turbulent variants.

### 2D FBM (generic, any noise type)

```glsl
// noise_func should be one of: value_noise, perlin_noise, cellular_euclidean, etc.
// For production use, replace the $noise call with the specific function.
float fbm(vec2 coord, vec2 size, int octaves, float persistence, int folds, float seed) {
    float value = 0.0, scale = 1.0, normalize_factor = 0.0;
    for (int i = 0; i < octaves; i++) {
        float noise = value_noise(coord * size, size, seed); // swap noise function here
        for (int f = 0; f < folds; ++f) {
            noise = abs(2.0 * noise - 1.0);
        }
        value += noise * scale;
        normalize_factor += scale;
        size *= 2.0;
        scale *= persistence;
    }
    return value / normalize_factor;
}
```

### 3D FBM (for triplanar / volumetric)

```glsl
float value_noise_3d(vec3 coord, vec3 size, float seed) {
    vec3 o = floor(coord) + rand3(vec2(seed, 1.0 - seed)) + size;
    vec3 f = fract(coord);
    float p000 = rand31(mod(o, size));
    float p001 = rand31(mod(o + vec3(0.0, 0.0, 1.0), size));
    float p010 = rand31(mod(o + vec3(0.0, 1.0, 0.0), size));
    float p011 = rand31(mod(o + vec3(0.0, 1.0, 1.0), size));
    float p100 = rand31(mod(o + vec3(1.0, 0.0, 0.0), size));
    float p101 = rand31(mod(o + vec3(1.0, 0.0, 1.0), size));
    float p110 = rand31(mod(o + vec3(1.0, 1.0, 0.0), size));
    float p111 = rand31(mod(o + vec3(1.0, 1.0, 1.0), size));
    vec3 t = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(p000, p100, t.x), mix(p010, p110, t.x), t.y),
               mix(mix(p001, p101, t.x), mix(p011, p111, t.x), t.y), t.z);
}

float fbm_3d(vec3 coord, vec3 size, int octaves, float persistence, float seed) {
    float value = 0.0, scale = 1.0, normalize_factor = 0.0;
    for (int i = 0; i < octaves; i++) {
        value += value_noise_3d(coord * size, size, seed) * scale;
        normalize_factor += scale;
        size *= 2.0;
        scale *= persistence;
    }
    return value / normalize_factor;
}
```

### Perlin 3D (gradient noise)

```glsl
float perlin_noise_3d(vec3 coord, vec3 size, float seed) {
    vec3 o = floor(coord) + rand3(vec2(seed, 1.0 - seed)) + size;
    vec3 f = fract(coord);
    vec3 v000 = normalize(rand33(mod(o, size)) - vec3(0.5));
    vec3 v001 = normalize(rand33(mod(o + vec3(0.0, 0.0, 1.0), size)) - vec3(0.5));
    vec3 v010 = normalize(rand33(mod(o + vec3(0.0, 1.0, 0.0), size)) - vec3(0.5));
    vec3 v011 = normalize(rand33(mod(o + vec3(0.0, 1.0, 1.0), size)) - vec3(0.5));
    vec3 v100 = normalize(rand33(mod(o + vec3(1.0, 0.0, 0.0), size)) - vec3(0.5));
    vec3 v101 = normalize(rand33(mod(o + vec3(1.0, 0.0, 1.0), size)) - vec3(0.5));
    vec3 v110 = normalize(rand33(mod(o + vec3(1.0, 1.0, 0.0), size)) - vec3(0.5));
    vec3 v111 = normalize(rand33(mod(o + vec3(1.0, 1.0, 1.0), size)) - vec3(0.5));
    float p000 = dot(v000, f);
    float p001 = dot(v001, f - vec3(0.0, 0.0, 1.0));
    float p010 = dot(v010, f - vec3(0.0, 1.0, 0.0));
    float p011 = dot(v011, f - vec3(0.0, 1.0, 1.0));
    float p100 = dot(v100, f - vec3(1.0, 0.0, 0.0));
    float p101 = dot(v101, f - vec3(1.0, 0.0, 1.0));
    float p110 = dot(v110, f - vec3(1.0, 1.0, 0.0));
    float p111 = dot(v111, f - vec3(1.0, 1.0, 1.0));
    vec3 t = f * f * (3.0 - 2.0 * f);
    return 0.5 + mix(mix(mix(p000, p100, t.x), mix(p010, p110, t.x), t.y),
                     mix(mix(p001, p101, t.x), mix(p011, p111, t.x), t.y), t.z);
}
```

### Usage Guidelines

| Noise Type | Best For | Cost |
|---|---|---|
| Value | displacement, roughness variation, cheap fill | Low |
| Perlin | terrain height, clouds, organic flow | Medium |
| Cellular F1 | scales, cobblestones, cell patterns | Medium |
| Cellular F2-F1 | cracks, veins, mortar lines | Medium |
| Wavelet | non-repetitive organic, large-scale variation | Medium-High |
| Anisotropic | brushed metal, wood grain, hair | Low |

| FBM Parameter | Effect |
|---|---|
| Octaves 1-3 | Smooth, broad shapes |
| Octaves 4-6 | Natural detail (terrain, clouds) |
| Octaves 7+ | Extreme detail (performance cost) |
| Persistence 0.3-0.5 | Natural falloff |
| Persistence 0.7-1.0 | Harsh, detailed |
| Folds 1 | Ridged/turbulent (mountain ridges) |
| Folds 2+ | Increasingly sharp ridges |

---

## SDF 2D Primitives

Signed distance functions return negative inside, zero on boundary, positive outside. Use for procedural masks, shapes, transitions, and UI effects.

```glsl
// Circle: center at origin, radius r
float sd_circle(vec2 p, float r) {
    return length(p) - r;
}

// Box: center at origin, half-extents b
float sd_box(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

// Rounded box: box with corner radius r
float sd_rounded_box(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + vec2(r);
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
}

// Regular polygon: n sides, radius r
float sd_polygon(vec2 p, float n, float r) {
    float angle = atan(p.x, p.y) + 3.14159265359;
    float slice = 6.28318530718 / n;
    return cos(floor(0.5 + angle / slice) * slice - angle) * length(p) - r;
}

// Star: n points, outer radius r
float sd_star(vec2 p, float n, float r) {
    float angle = atan(p.x, p.y);
    float slice = 6.28318530718 / n;
    float k = floor(angle * n / 6.28318530718 - 0.5 + 2.0 * step(fract(angle * n / 6.28318530718), 0.5));
    return cos(k * slice - angle) * length(p) - r;
}

// Line segment: from a to b, returns distance
float sd_segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Annular (ring): take any SDF, subtract inner radius
float sd_annular(float d, float thickness) {
    return abs(d) - thickness;
}
```

### SDF Rendering Utilities

```glsl
// Sharp edge at distance 0
float sdf_sharp(float d) {
    return step(0.0, -d);
}

// Smooth anti-aliased edge
float sdf_smooth(float d, float edge_width) {
    return clamp(-d / edge_width, 0.0, 1.0);
}

// Outline only
float sdf_outline(float d, float width) {
    return clamp(1.0 - abs(d) / width, 0.0, 1.0);
}
```

---

## SDF 3D Primitives

For raymarching materials and procedural 3D shapes. All centered at origin.

```glsl
float sd_sphere(vec3 p, float r) {
    return length(p) - r;
}

float sd_box_3d(vec3 p, vec3 b) {
    vec3 d = abs(p) - b;
    return length(max(d, vec3(0.0))) + min(max(d.x, max(d.y, d.z)), 0.0);
}

float sd_cylinder(vec3 p, float r, float h) {
    vec2 d = vec2(length(p.xz) - r, abs(p.y) - h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2(0.0)));
}

float sd_torus(vec3 p, float major_r, float minor_r) {
    vec2 q = vec2(length(p.xz) - major_r, p.y);
    return length(q) - minor_r;
}

float sd_capsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float sd_cone(vec3 p, float angle, float h) {
    vec2 c = vec2(sin(angle), cos(angle));
    vec2 q = h * vec2(c.x / c.y, -1.0);
    vec2 w = vec2(length(p.xz), p.y);
    vec2 a = w - q * clamp(dot(w, q) / dot(q, q), 0.0, 1.0);
    vec2 b = w - q * vec2(clamp(w.x / q.x, 0.0, 1.0), 1.0);
    float k = sign(q.y);
    float d = min(dot(a, a), dot(b, b));
    float s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

float sd_plane(vec3 p, vec3 n, float d) {
    return dot(p, normalize(n)) + d;
}
```

---

## SDF Operations

### Boolean Operations

```glsl
// Hard boolean
float sdf_union(float d1, float d2) { return min(d1, d2); }
float sdf_subtract(float d1, float d2) { return max(-d1, d2); }
float sdf_intersect(float d1, float d2) { return max(d1, d2); }

// Smooth boolean (k = smoothness, typically 0.1-0.5)
float smin(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

float smax(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) + k * h * (1.0 - h);
}

// With color index propagation (for raymarching materials)
vec2 sdf3dc_union(vec2 a, vec2 b) {
    return vec2(min(a.x, b.x), mix(b.y, a.y, step(a.x, b.x)));
}
vec2 sdf3dc_subtract(vec2 a, vec2 b) {
    return vec2(max(-a.x, b.x), a.y);
}
vec2 sdf3dc_intersect(vec2 a, vec2 b) {
    return vec2(max(a.x, b.x), mix(a.y, b.y, step(a.x, b.x)));
}
```

### Transform Operations

```glsl
// Repetition (infinite tiling)
vec3 sdf_repeat(vec3 p, vec3 spacing) {
    return mod(p + 0.5 * spacing, spacing) - 0.5 * spacing;
}

// Finite repetition (clamped)
vec3 sdf_repeat_clamped(vec3 p, vec3 spacing, vec3 count) {
    return p - spacing * clamp(round(p / spacing), -count, count);
}

// Round any SDF (add radius)
float sdf_round(float d, float r) { return d - r; }

// Elongation (stretch along axis)
float sd_elongate(vec3 p, vec3 h, float sdf_d) {
    vec3 q = abs(p) - h;
    // Use: sd_something(max(q, vec3(0.0)), ...) + min(max(q.x, max(q.y, q.z)), 0.0)
    return sdf_d; // placeholder — apply to the primitive
}
```

### SDF FBM (noise on SDF surfaces)

Based on Inigo Quilez's technique for adding fractal detail to SDF shapes.

```glsl
// Adds FBM noise to an existing SDF distance value
// d = input SDF distance, p = world position
float sdf_fbm(vec3 p, float d, int iterations, float smoothness, float seed) {
    float s = 1.0;
    for (int i = 0; i < iterations; i++) {
        float n = s * sd_base_noise(p, seed); // use value_noise_3d or similar
        n = smax(n, d - 0.1 * s, smoothness * s);
        d = smin(n, d, smoothness * s);
        // Rotate for each octave to break axis alignment
        p = mat3(vec3(0.02, 1.60, 1.20),
                 vec3(-1.60, 0.72, -0.96),
                 vec3(-1.20, -0.96, 1.28)) * p;
        s = 0.5 * s;
    }
    return d;
}
```

---

## Blend Modes

All blend functions take foreground `c1`, background `c2`, and `opacity` [0,1].

```glsl
vec3 blend_normal(vec3 c1, vec3 c2, float opacity) {
    return opacity * c1 + (1.0 - opacity) * c2;
}

vec3 blend_multiply(vec3 c1, vec3 c2, float opacity) {
    return opacity * c1 * c2 + (1.0 - opacity) * c2;
}

vec3 blend_screen(vec3 c1, vec3 c2, float opacity) {
    return opacity * (1.0 - (1.0 - c1) * (1.0 - c2)) + (1.0 - opacity) * c2;
}

vec3 blend_overlay(vec3 c1, vec3 c2, float opacity) {
    vec3 r = vec3(
        c2.x < 0.5 ? 2.0 * c1.x * c2.x : 1.0 - 2.0 * (1.0 - c1.x) * (1.0 - c2.x),
        c2.y < 0.5 ? 2.0 * c1.y * c2.y : 1.0 - 2.0 * (1.0 - c1.y) * (1.0 - c2.y),
        c2.z < 0.5 ? 2.0 * c1.z * c2.z : 1.0 - 2.0 * (1.0 - c1.z) * (1.0 - c2.z));
    return opacity * r + (1.0 - opacity) * c2;
}

vec3 blend_soft_light(vec3 c1, vec3 c2, float opacity) {
    vec3 r = vec3(
        c2.x < 0.5 ? 2.0*c1.x*c2.x + c1.x*c1.x*(1.0-2.0*c2.x) : 2.0*c1.x*(1.0-c2.x) + sqrt(c1.x)*(2.0*c2.x-1.0),
        c2.y < 0.5 ? 2.0*c1.y*c2.y + c1.y*c1.y*(1.0-2.0*c2.y) : 2.0*c1.y*(1.0-c2.y) + sqrt(c1.y)*(2.0*c2.y-1.0),
        c2.z < 0.5 ? 2.0*c1.z*c2.z + c1.z*c1.z*(1.0-2.0*c2.z) : 2.0*c1.z*(1.0-c2.z) + sqrt(c1.z)*(2.0*c2.z-1.0));
    return opacity * r + (1.0 - opacity) * c2;
}

vec3 blend_burn(vec3 c1, vec3 c2, float opacity) {
    vec3 r = vec3(
        c1.x == 0.0 ? 0.0 : max(1.0 - (1.0 - c2.x) / c1.x, 0.0),
        c1.y == 0.0 ? 0.0 : max(1.0 - (1.0 - c2.y) / c1.y, 0.0),
        c1.z == 0.0 ? 0.0 : max(1.0 - (1.0 - c2.z) / c1.z, 0.0));
    return opacity * r + (1.0 - opacity) * c2;
}

vec3 blend_dodge(vec3 c1, vec3 c2, float opacity) {
    vec3 r = vec3(
        c1.x == 1.0 ? 1.0 : min(c2.x / (1.0 - c1.x), 1.0),
        c1.y == 1.0 ? 1.0 : min(c2.y / (1.0 - c1.y), 1.0),
        c1.z == 1.0 ? 1.0 : min(c2.z / (1.0 - c1.z), 1.0));
    return opacity * r + (1.0 - opacity) * c2;
}

vec3 blend_lighten(vec3 c1, vec3 c2, float opacity) {
    return opacity * max(c1, c2) + (1.0 - opacity) * c2;
}

vec3 blend_darken(vec3 c1, vec3 c2, float opacity) {
    return opacity * min(c1, c2) + (1.0 - opacity) * c2;
}

vec3 blend_difference(vec3 c1, vec3 c2, float opacity) {
    return opacity * abs(c2 - c1) + (1.0 - opacity) * c2;
}

vec3 blend_additive(vec3 c1, vec3 c2, float opacity) {
    return c2 + c1 * opacity;
}

vec3 blend_addsub(vec3 c1, vec3 c2, float opacity) {
    return c2 + (c1 - 0.5) * 2.0 * opacity;
}
```

---

## Color Utilities

```glsl
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = c.g < c.b ? vec4(c.bg, K.wz) : vec4(c.gb, K.xy);
    vec4 q = c.r < p.x ? vec4(p.xyw, c.r) : vec4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Height to normal (Sobel-like, 8-sample)
vec3 height_to_normal(sampler2D height_tex, vec2 uv, float texel_size, float strength) {
    vec3 e = vec3(texel_size, -texel_size, 0.0);
    vec2 rv = vec2(1.0, -1.0) * texture(height_tex, uv + e.xy).r;
    rv += vec2(-1.0, 1.0) * texture(height_tex, uv - e.xy).r;
    rv += vec2(1.0, 1.0) * texture(height_tex, uv + e.xx).r;
    rv += vec2(-1.0, -1.0) * texture(height_tex, uv - e.xx).r;
    rv += vec2(2.0, 0.0) * texture(height_tex, uv + e.xz).r;
    rv += vec2(-2.0, 0.0) * texture(height_tex, uv - e.xz).r;
    rv += vec2(0.0, 2.0) * texture(height_tex, uv + e.zx).r;
    rv += vec2(0.0, -2.0) * texture(height_tex, uv - e.zx).r;
    return normalize(vec3(rv * strength, -1.0)) * 0.5 + 0.5;
}

// Procedural height to normal (no texture, uses function evaluation)
// Call with: height_to_normal_proc(uv, texel_size, strength)
// where get_height(uv) is your procedural height function
// vec3 height_to_normal_proc(vec2 uv, float eps, float strength) {
//     float h_r = get_height(uv + vec2(eps, 0.0));
//     float h_l = get_height(uv - vec2(eps, 0.0));
//     float h_u = get_height(uv + vec2(0.0, eps));
//     float h_d = get_height(uv - vec2(0.0, eps));
//     return normalize(vec3(h_l - h_r, h_d - h_u, 2.0 * eps / strength)) * 0.5 + 0.5;
// }
```
