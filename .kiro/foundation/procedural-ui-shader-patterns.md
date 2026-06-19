# Procedural UI Shader Patterns — Knowledge from Material Maker

Distilled patterns, techniques, and learnings from Material Maker's procedural shader system that can improve the Procedural Shader Utility addon for UI rendering.

---

## 1. SDF Primitives — Better Implementations

### Per-Corner Rounded Box (Inigo Quilez's Optimal Version)

Material Maker uses this compact formulation that handles per-corner radii in a single expression without branching:

```glsl
float sdRoundedBox(vec2 p, vec2 b, vec4 r) {
    // r = vec4(top_right, bottom_right, top_left, bottom_left)
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}
```

**Key insight**: The corner selection uses ternary operators (which GPUs handle as conditional moves, not branches). This is the most efficient known per-corner rounded rect SDF — no loops, no arrays.

**Improvement opportunity**: If the addon's current implementation differs, this is the gold standard to match.

### Capsule (Segment-Based)

```glsl
float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}
```

This generalizes to arbitrary-angle capsules, not just axis-aligned. Could enable rotated pill shapes.

---

## 2. SDF Operations — Patterns Worth Adopting

### Annular / Ripple Operation

Material Maker has a powerful `sdRipples` operation that creates concentric rings from any SDF:

```glsl
float sdRipples(float d, float width, int ripples) {
    for (int i = 0; i < ripples; ++i) {
        d = abs(d) - width;
    }
    return d;
}
```

**UI use cases**:
- Multi-ring focus indicators
- Concentric badge borders
- Radio button / checkbox ring states
- Loading spinners with multiple rings

**Improvement opportunity**: Add as a new operation type alongside UNION/SUBTRACT/INTERSECT. Or expose as a post-processing step on the final SDF before rendering.

### SDF Offset (Grow/Shrink)

```glsl
float sdOffset(float d, float amount) {
    return d - amount; // negative = grow, positive = shrink
}
```

**UI use cases**:
- Animated focus ring that grows outward from the shape
- Press state that shrinks the shape slightly
- Hover state with expanded hit area visualization

### SDF Onion (Hollow Shape)

```glsl
float sdOnion(float d, float thickness) {
    return abs(d) - thickness;
}
```

**Difference from stroke**: Stroke in the addon is rendered as a separate paint pass. Onion modifies the SDF itself, creating a hollow shape that can then participate in further boolean operations. This enables:
- Hollow shapes that can be intersected/subtracted with other shapes
- Ring shapes that are first-class SDF citizens

---

## 3. Smooth Boolean — Material Maker's Formulations

The addon already has smooth booleans. Here are Material Maker's exact implementations for reference/comparison:

```glsl
float sdSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

float sdSmoothSubtraction(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 + d1) / k, 0.0, 1.0);
    return mix(d2, -d1, h) + k * h * (1.0 - h);
}

float sdSmoothIntersection(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) + k * h * (1.0 - h);
}
```

**Note**: The subtraction formula uses `(d2 + d1)` not `(d2 - d1)` — this is the correct Inigo Quilez formulation. Verify the addon matches.

---

## 4. Gradient & Color Patterns

### Gradient Types from Material Maker

Material Maker supports these gradient coordinate mappings:

```glsl
// Linear (already in addon)
float t_linear = uv.x; // rotated by gradient_rotation

// Radial (already in addon)
float t_radial = length(uv - center);

// Angular / Conical
float t_angular = (atan(uv.y - center.y, uv.x - center.x) + PI) / TAU;

// Diamond
float t_diamond = abs(uv.x - center.x) + abs(uv.y - center.y);

// Square (not in addon — useful for UI frames)
float t_square = max(abs(uv.x - center.x), abs(uv.y - center.y));
```

**Improvement opportunity**: Add `SQUARE` gradient type — it follows the shape of a rectangle and is natural for UI panel borders/frames.

### Multi-Stop Gradient Optimization

Material Maker bakes gradients into `GradientTexture1D` and samples with a single `texture()` call. This is optimal for complex multi-stop gradients. For simple 2-color gradients, a `mix()` is cheaper than a texture fetch.

**Improvement opportunity**: If the addon always uses texture sampling for gradients, consider a fast path for 2-color gradients that uses `mix(color_a, color_b, t)` directly — saves a texture fetch per pixel.

### HSV Color Utilities

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
```

**UI use cases**:
- Hue-shift variants without creating separate gradient resources
- Animated rainbow/iridescent effects on UI elements
- Programmatic color theming (shift hue of base style)

---

## 5. Anti-Aliasing Techniques

### Material Maker's SDF Rendering

```glsl
// Their "sdShow" node renders SDF to grayscale:
float mask = clamp(base - dist / max(bevel, 0.00001), 0.0, 1.0);
```

The `bevel` parameter controls edge softness — equivalent to the addon's `feather_px`.

### Screen-Space Adaptive AA

```glsl
float aa = fwidth(dist); // screen-space derivative of the distance field
float mask = smoothstep(aa, -aa, dist);
```

This automatically adapts edge softness to screen resolution and zoom level. The addon already uses `fwidth()` as fallback — this confirms it's the correct approach.

### Combining Feather + fwidth

Best practice (which the addon already does):

```glsl
float effective_feather = max(feather, fwidth(dist));
float mask = smoothstep(effective_feather, -effective_feather, dist);
```

This ensures edges are never thinner than one screen pixel, regardless of the feather setting.

---

## 6. Procedural Patterns for UI Backgrounds

These patterns from Material Maker could be useful as optional fill modes beyond solid/gradient:

### Noise Fill

```glsl
float rand(vec2 x) {
    return fract(cos(mod(dot(x, vec2(13.9898, 8.141)), 3.14)) * 43758.5);
}

float value_noise(vec2 uv, vec2 size) {
    vec2 o = floor(uv * size);
    vec2 f = fract(uv * size);
    float p00 = rand(mod(o, size));
    float p10 = rand(mod(o + vec2(1.0, 0.0), size));
    float p01 = rand(mod(o + vec2(0.0, 1.0), size));
    float p11 = rand(mod(o + vec2(1.0, 1.0), size));
    vec2 t = f * f * (3.0 - 2.0 * f);
    return mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);
}
```

**UI use cases**:
- Subtle paper/fabric texture on cards
- Frosted glass effect backgrounds
- Animated shimmer/sparkle on premium UI elements

### Dot Pattern / Halftone

```glsl
float dot_pattern(vec2 uv, float scale, float radius) {
    vec2 grid = fract(uv * scale) - 0.5;
    return smoothstep(radius, radius - 0.02, length(grid));
}
```

**UI use cases**:
- Disabled state overlay (halftone dimming)
- Decorative panel backgrounds
- Progress indicators

### Stripe Pattern

```glsl
float stripes(vec2 uv, float angle, float frequency, float width) {
    float ca = cos(angle), sa = sin(angle);
    float t = ca * uv.x + sa * uv.y;
    return smoothstep(width, width + 0.01, abs(fract(t * frequency) - 0.5));
}
```

**UI use cases**:
- "Under construction" / warning panels
- Decorative borders
- Loading state indicators

---

## 7. Shadow & Glow Patterns

### Drop Shadow via SDF Offset

```glsl
// Evaluate the same SDF at an offset position with expanded radius
float shadow_dist = sdRoundedBox(uv - shadow_offset, box_size, corner_radii);
float shadow = smoothstep(shadow_blur, 0.0, shadow_dist);
// Render shadow first (behind), then shape on top
```

**Improvement opportunity**: The addon could support drop shadows as a paint property. The SDF is already computed — evaluating it at an offset position is nearly free. Parameters: `shadow_offset`, `shadow_blur`, `shadow_color`, `shadow_opacity`.

### Inner Glow

```glsl
// Use positive distance (inside the shape) for inner glow
float inner_glow = smoothstep(0.0, glow_radius, dist) * glow_intensity;
// Mix glow color into fill based on inner_glow
```

### Outer Glow

```glsl
// Use negative distance (outside the shape) for outer glow
float outer_glow = smoothstep(glow_radius, 0.0, -dist) * glow_intensity;
// Render glow as a separate layer behind the shape
```

**Improvement opportunity**: Inner/outer glow as paint properties. Very common in UI design tools (Figma, Sketch).

---

## 8. Transform Operations on SDF

### Rotation

```glsl
vec2 sdf_rotate(vec2 p, float angle) {
    float ca = cos(angle), sa = sin(angle);
    return vec2(ca * p.x + sa * p.y, -sa * p.x + ca * p.y);
}
```

**Improvement opportunity**: Per-primitive rotation. Currently primitives can be positioned and sized but not rotated. Rotation would enable:
- Rotated rectangles for decorative elements
- Diamond shapes (45° rotated square)
- Angled cutouts

### Symmetry

```glsl
vec2 sdf_mirror_x(vec2 p) { return vec2(abs(p.x), p.y); }
vec2 sdf_mirror_y(vec2 p) { return vec2(p.x, abs(p.y)); }
```

**UI use cases**: Build symmetric shapes with half the primitives (e.g., define one side of a symmetric cutout).

---

## 9. Performance Insights from Material Maker

### Shader Complexity Budget

Material Maker's node system generates shaders with potentially hundreds of operations. Their key performance learnings:

1. **SDF evaluation is cheap** — a single rounded rect SDF is ~5 ALU ops. Even 4 primitives with booleans is ~25 ALU ops. This is well within budget for UI.

2. **Texture fetches are expensive relative to ALU** — one gradient texture sample costs more than several SDF evaluations. The 2-color `mix()` fast path matters.

3. **`fwidth()` is essentially free** — it's computed from existing partial derivatives that the GPU already has.

4. **Smooth booleans add ~3 ALU ops each** — negligible cost for the visual improvement.

5. **The real cost is overdraw** — multiple overlapping transparent panels each run the full shader. This is the primary performance concern, not shader complexity.

### Precompilation Strategy

Material Maker uses file-backed shaders (not runtime string generation) for the same reason the addon does — Godot's shader pipeline cache works with file-backed shaders. Runtime-generated shader strings cause first-use compilation stalls.

---

## 10. Feature Ideas Derived from Material Maker

### Priority 1 — Low effort, high value

| Feature | What | Why |
|---------|------|-----|
| Drop shadow | SDF evaluated at offset + blur | Most requested UI effect, nearly free with existing SDF |
| Outer glow | Smoothstep on negative distance | Common in Figma/Sketch designs |
| 2-color gradient fast path | `mix()` instead of texture fetch | Performance win for simple gradients |
| SDF offset operation | `d - amount` | Animated grow/shrink for hover/press states |

### Priority 2 — Medium effort, high value

| Feature | What | Why |
|---------|------|-----|
| Per-primitive rotation | Rotate UV before SDF eval | Diamond shapes, angled cutouts |
| Annular/ripple operation | `abs(d) - width` repeated | Multi-ring focus indicators, radio buttons |
| Inner glow | Smoothstep on positive distance | Inset lighting effects |
| Square gradient type | `max(abs(x), abs(y))` | Natural for rectangular UI frames |

### Priority 3 — Higher effort, niche value

| Feature | What | Why |
|---------|------|-----|
| Noise fill mode | Inline value noise | Paper/fabric texture, frosted glass |
| Dot/stripe patterns | Procedural pattern fills | Disabled states, decorative backgrounds |
| HSV color shift | Runtime hue rotation | Programmatic theming without new resources |
| SDF morphing | Interpolate between two SDFs | Animated shape transitions (circle→rect) |

---

## 11. SDF Morphing (Shape Transitions)

Material Maker's `sdmorph` node linearly interpolates between two SDFs:

```glsl
float sdMorph(float d1, float d2, float t) {
    return mix(d1, d2, t);
}
```

**UI use cases**:
- Toggle switch: morph between circle and rounded rect
- Tab selection: morph between sharp rect and rounded rect
- State transitions: morph between shapes for different states

**Implementation note**: This only works well when both SDFs have similar scale. Morphing between very different sizes produces intermediate states that may not look like either shape.

---

## 12. Aspect Ratio Handling

Material Maker normalizes all SDF coordinates to [0,1] UV space and handles aspect ratio via explicit `size` parameters. The addon's approach of using `min(w,h)` for pixel-to-local conversion is sound but creates the documented issue of non-uniform stroke on elongated panels.

**Alternative approach from Material Maker**: Separate X and Y scale factors:

```glsl
// Instead of dividing by min(w,h), use separate axes:
vec2 pixel_to_local = vec2(1.0 / size.x, 1.0 / size.y);
// Stroke in local space:
float stroke_local = stroke_px * min(pixel_to_local.x, pixel_to_local.y);
```

This preserves uniform stroke width regardless of aspect ratio. The tradeoff is that corner radii behavior changes on non-square panels.

---

## Summary

The most impactful additions from Material Maker's patterns for the procedural UI addon are:

1. **Drop shadow** (SDF at offset) — universally needed, trivial to implement
2. **Outer/inner glow** — common design tool feature
3. **Annular/ripple operation** — enables multi-ring UI patterns
4. **Per-primitive rotation** — unlocks diamond shapes and angled elements
5. **2-color gradient fast path** — free performance win
6. **SDF offset for animated grow/shrink** — natural hover/press feedback

All of these build on the existing SDF infrastructure without requiring architectural changes.
