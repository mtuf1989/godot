# Pattern Catalog

Use this file first when choosing or reviewing a `canvas_item` shader pattern.

## Shared Rules

- Stay inside the `canvas_item` boundary:
  - gameplay sprites
  - UI surfaces
  - full-screen overlay `ColorRect` transitions
- Treat the code snippets below as core math sketches, not full drop-in shaders.
- Default to these implementation rules unless grounded project facts force a change:
  - `shader_type canvas_item;`
  - snake_case for uniforms, locals, and helper functions
  - `group_uniforms` for inspector organization
  - `source_color` on exposed colors
  - `instance uniform` for runtime values that vary per entity
  - branch-light math with `mix()`, `step()`, `smoothstep()`, and `clamp()`
  - preserve alpha intentionally
- Treat these as integration hazards:
  - `AtlasTexture` or region-backed sprites
  - palette LUT filtering
  - outline padding at texture edges
  - multi-tap sampling cost
  - overdraw on full-screen effects

## Pick A Pattern

### Hit Flash

Use when:

- an enemy, player, or destructible needs immediate damage feedback
- one logical entity should flash without cloning the whole material per instance

Typical host:

- `Sprite2D`
- `AnimatedSprite2D`
- a parent `CanvasGroup` when several child sprites should flash together

Core uniforms:

- `instance uniform vec4 flash_color : source_color`
- `instance uniform float flash_modifier : hint_range(0.0, 1.0)`

Core fragment strategy:

```glsl
vec4 base = texture(TEXTURE, UV);
COLOR.rgb = mix(base.rgb, flash_color.rgb, flash_modifier);
COLOR.a = base.a;
```

Premultiplied alpha variant (use when the project or blend mode expects premultiplied output):

```glsl
vec4 base = texture(TEXTURE, UV);
vec3 flashed = mix(base.rgb, flash_color.rgb * base.a, flash_modifier);
COLOR = vec4(flashed, base.a);
```

Watch for:

- transparent-box bleeding when premultiplied alpha rules are ignored — if the project uses premultiplied blending, multiply the flash color RGB by `base.a` before output or the flash will fill the transparent bounding box
- all instances flashing together because `flash_modifier` was not an `instance uniform`

Validation:

- tween `flash_modifier` on one instance in a crowd
- confirm only the targeted entity flashes
- confirm transparent pixels stay transparent

### Dissolve Or Materialize

Use when:

- an entity should appear or disappear with noise-driven breakup instead of plain alpha fade
- a death, teleport, summon, or burn edge needs a bounded reusable effect

Typical host:

- `Sprite2D`
- `AnimatedSprite2D`
- a parent `CanvasGroup` when several child sprites should dissolve together as one logical entity
- other textured `CanvasItem` nodes with predictable UV mapping

Core uniforms:

- `uniform sampler2D dissolve_texture : hint_default_white`
- `instance uniform float dissolve_amount : hint_range(0.0, 1.0)`
- `uniform vec4 edge_color : source_color`
- `uniform float edge_thickness : hint_range(0.0, 0.5)`

Core fragment strategy:

```glsl
// scale noise UV by sprite pixel size to keep frequency consistent across differently sized sprites
vec2 dissolve_uv = UV * (1.0 / TEXTURE_PIXEL_SIZE) / noise_scale;
float noise = texture(dissolve_texture, dissolve_uv).r;
float visible = step(dissolve_amount, noise);
float edge = smoothstep(dissolve_amount, dissolve_amount + edge_thickness, noise);
```

Watch for:

- noise scale stretching across differently sized sprites — scale the noise UV by the sprite's pixel dimensions (via `TEXTURE_PIXEL_SIZE`) or pass a manual scale uniform so the noise frequency stays consistent regardless of sprite size
- edge glow clipping when the source texture has no transparent padding
- atlas bleed if generated or remapped UV logic ignores local sprite regions

Validation:

- animate `dissolve_amount` from `0.0` to `1.0`
- confirm the sprite fully disappears with no neighboring atlas pixels leaking in
- confirm edge thickness still reads consistently

### Outline Or Glow

Use when:

- interactables, selected units, or readable pickups need a contour
- the effect should live on the sprite rather than in a separate mesh or duplicate sprite stack

Typical host:

- `Sprite2D`
- `AnimatedSprite2D`
- small item sprites or selection targets

Core uniforms:

- `instance uniform vec4 outline_color : source_color`
- `uniform float outline_width : hint_range(0.0, 10.0)`
- `instance uniform bool enable_outline`

Core fragment strategy (4-tap cardinal sample):

```glsl
vec2 ps = TEXTURE_PIXEL_SIZE * outline_width;
float center_alpha = texture(TEXTURE, UV).a;
float neighbor_alpha = 0.0;
neighbor_alpha += texture(TEXTURE, UV + vec2(ps.x, 0.0)).a;
neighbor_alpha += texture(TEXTURE, UV + vec2(-ps.x, 0.0)).a;
neighbor_alpha += texture(TEXTURE, UV + vec2(0.0, ps.y)).a;
neighbor_alpha += texture(TEXTURE, UV + vec2(0.0, -ps.y)).a;
float edge = step(0.01, neighbor_alpha) * (1.0 - center_alpha);
```

For 8-tap (cardinal + diagonal), add the four diagonal offsets (`±ps.x, ±ps.y`) for smoother coverage at higher cost.

Soft-edge variant for anti-aliased or high-resolution art:

```glsl
// replace the hard step with smoothstep to avoid jagged outlines on soft alpha edges
float edge = smoothstep(0.0, 0.5, neighbor_alpha) * (1.0 - smoothstep(0.0, 0.1, center_alpha));
```

Watch for:

- clipped outlines when opaque pixels touch the texture border — the asset needs transparent padding or the effect needs a slightly oversized proxy host
- jagged edges on soft or antialiased art if the logic uses only hard binary alpha tests — use the `smoothstep` variant above for gradual alpha boundaries
- unnecessary GPU cost if the multi-tap path stays active when the outline is visually off — gate the expensive path behind `enable_outline`

Validation:

- test sprites with and without transparent padding
- inspect every extreme edge for flat clipping
- toggle the outline off and confirm the heavy path is bypassed

### Palette Swap

Use when:

- one sprite sheet should represent multiple factions, skins, or variants
- the project wants recolors without duplicating textures

Typical host:

- grayscale or palette-authored gameplay sprites
- `Sprite2D` or animated atlas-backed sprites, with extra care around filtering

Core uniforms:

- `uniform sampler2D palette_texture : filter_nearest`
- `instance uniform float palette_index`

Core fragment strategy:

```glsl
float shade = texture(TEXTURE, UV).r;
vec4 mapped = texture(palette_texture, vec2(shade, palette_index));
```

Watch for:

- muddy blended colors because the LUT is not using nearest filtering
- brittle exact-color equality checks instead of stable shade-to-LUT mapping
- unintended alpha changes if the original sprite alpha is not preserved

Validation:

- cycle across multiple palette rows
- confirm there is no row bleed or color interpolation
- confirm the original sprite alpha remains intact

### Gradient Background Or Panel

Use when:

- UI backgrounds, menus, sky-like backdrops, or resizable panels need procedural color transitions
- a static texture would be less flexible than a small shader

Typical host:

- `ColorRect`
- `TextureRect`
- other rectangular UI surfaces

Core uniforms:

- `instance uniform vec4 color_top : source_color`
- `instance uniform vec4 color_bottom : source_color`
- `uniform float gradient_offset : hint_range(-1.0, 1.0)`

Core fragment strategy:

```glsl
float t = clamp(UV.y + gradient_offset, 0.0, 1.0);
COLOR = mix(color_top, color_bottom, t);
```

Anti-banding dithering addition (apply after the gradient mix):

```glsl
// Bayer 2x2 dither to break 8-bit banding on subtle gradients
float bayer = mod(floor(FRAGCOORD.x) + 2.0 * floor(FRAGCOORD.y), 4.0) / 4.0;
COLOR.rgb += (bayer - 0.5) / 255.0;
```

Watch for:

- visible banding on subtle dark gradients — add the dithering snippet above or a similar noise-based offset to break 8-bit color stepping
- layout assumptions that break when the host node changes size
- using a full-screen gradient when a smaller local panel would do and overdraw would be lower

Validation:

- stretch the host across a large viewport
- test near-similar colors that provoke banding
- confirm the dithering logic improves the result instead of adding obvious patterned noise

### Screen Transition

Use when:

- scene changes, menus, or game states need a stylized full-screen wipe or reveal
- the effect should be owned by a dedicated UI overlay instead of sprinkled across gameplay nodes

Typical host:

- full-rect `ColorRect` inside a dedicated high-layer `CanvasLayer`

Core uniforms:

- `uniform sampler2D transition_mask`
- `uniform float progress : hint_range(0.0, 1.0)`
- `uniform vec4 transition_color : source_color`

Core fragment strategy (texture-mask approach):

```glsl
float mask = texture(transition_mask, mask_uv).r;
COLOR = mix(vec4(0.0), transition_color, step(mask, progress));
```

Procedural diamond variant (no external texture needed):

```glsl
// correct for aspect ratio so diamonds stay square on any screen shape
float aspect = SCREEN_PIXEL_SIZE.y / SCREEN_PIXEL_SIZE.x;
vec2 corrected_uv = vec2(UV.x * aspect, UV.y);
float diamond_size = 8.0; // number of diamonds across the screen
vec2 grid = fract(corrected_uv * diamond_size);
float mask = abs(grid.x - 0.5) + abs(grid.y - 0.5);
COLOR = mix(vec4(0.0), transition_color, step(mask, progress));
```

Watch for:

- incorrect draw order because the overlay is not isolated in its own `CanvasLayer`
- stretched diamonds, circles, or wipes when aspect ratio is ignored in generated-shape math — use `SCREEN_PIXEL_SIZE` to derive the aspect ratio and correct UV.x before shape generation
- unnecessary heavy texture sampling on already full-screen work — prefer procedural `fract()`-based shapes when the pattern is geometric and regular

Validation:

- resize to tall and ultrawide aspect ratios
- confirm the transition stays above gameplay and UI
- confirm the generated shape keeps its intended proportions

### UV Distortion Or Warp

Use when:

- a local texture needs water wobble, heat haze, magical shimmer, or simple warp motion
- the effect can stay bounded to the node instead of requiring a full post-process

Typical host:

- standalone `Sprite2D`
- `TextureRect`
- controlled UI or effect surfaces that can tolerate local UV displacement

Core uniforms:

- `uniform sampler2D noise_texture : repeat_enable`
- `uniform vec2 speed`
- `uniform float strength`

Core fragment strategy:

```glsl
vec2 noise_uv = UV + TIME * speed;
vec2 offset = (texture(noise_texture, noise_uv).rg * 2.0 - 1.0) * strength;
vec4 warped = texture(TEXTURE, UV + offset);
```

Watch for:

- edge streaks or clamp artifacts when warped UVs leave the usable range
- broken scrolling if the noise texture is not repeat-enabled
- severe atlas bleed on packed sprite sheets

Validation:

- test on a standalone texture first
- then test on any atlas-backed target separately
- document the exact atlas limit honestly if the effect is unsafe there

### SDF Shape Mask

Use when:

- a transition, reveal, or clip needs a procedural geometric shape (circle wipe, rounded-rect reveal, star mask) without importing a texture
- UI elements need soft-edged procedural masks driven by shader math
- a sprite needs a shaped vignette, spotlight, or shaped fade

Typical host:

- `ColorRect` for full-screen transitions
- `Sprite2D` or `TextureRect` for shaped reveals
- any `CanvasItem` needing a procedural clip

Core uniforms:

- `instance uniform float progress : hint_range(0.0, 1.0)`
- `uniform float edge_softness : hint_range(0.001, 0.2)`
- `uniform vec4 mask_color : source_color`

Core SDF utilities (include in shader):

```glsl
float sd_circle(vec2 p, float r) {
    return length(p) - r;
}

float sd_box(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

float sd_rounded_box(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + vec2(r);
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
}

float sd_polygon(vec2 p, float sides, float r) {
    float angle = atan(p.x, p.y) + 3.14159265359;
    float slice = 6.28318530718 / sides;
    return cos(floor(0.5 + angle / slice) * slice - angle) * length(p) - r;
}
```

Core fragment strategy (circle wipe example):

```glsl
vec2 center_uv = UV * 2.0 - 1.0; // remap to [-1, 1]
// Aspect correction for non-square nodes
center_uv.x *= TEXTURE_PIXEL_SIZE.y / TEXTURE_PIXEL_SIZE.x;

float dist = sd_circle(center_uv, progress * 1.5);
float mask = smoothstep(0.0, edge_softness, -dist);

vec4 base = texture(TEXTURE, UV);
COLOR = mix(mask_color, base, mask);
```

Rounded-rect reveal variant:

```glsl
vec2 center_uv = UV * 2.0 - 1.0;
float dist = sd_rounded_box(center_uv, vec2(progress), 0.1);
float mask = smoothstep(0.0, edge_softness, -dist);
COLOR = vec4(base.rgb, base.a * mask);
```

Watch for:

- aspect ratio distortion on non-square nodes — correct UV.x by the pixel size ratio
- SDF shapes expanding beyond the node bounds — clamp or scale the progress range
- hard aliased edges when `edge_softness` is too small — keep it at least `0.005`

Validation:

- animate `progress` from 0 to 1
- test on both square and non-square nodes
- confirm the shape stays proportional at different aspect ratios

### Blend Mode Effect

Use when:

- two layers or textures need compositing with Photoshop-style blend modes inside a shader
- a dissolve, transition, or overlay effect needs multiply, screen, overlay, or other non-standard blending
- a sprite needs a procedural color adjustment (burn, dodge, soft light) driven by a mask or noise

Typical host:

- `Sprite2D` with a secondary texture input
- `ColorRect` for full-screen color grading overlays
- `CanvasGroup` for compositing child sprites

Core uniforms:

- `uniform sampler2D blend_texture : hint_default_white`
- `instance uniform float blend_opacity : hint_range(0.0, 1.0)`
- `uniform int blend_mode : hint_range(0, 5)` (or use separate shaders per mode)

Core blend functions:

```glsl
vec3 blend_multiply(vec3 base, vec3 blend, float opacity) {
    return opacity * base * blend + (1.0 - opacity) * base;
}

vec3 blend_screen(vec3 base, vec3 blend, float opacity) {
    return opacity * (1.0 - (1.0 - base) * (1.0 - blend)) + (1.0 - opacity) * base;
}

vec3 blend_overlay(vec3 base, vec3 blend, float opacity) {
    vec3 r = vec3(
        base.x < 0.5 ? 2.0 * base.x * blend.x : 1.0 - 2.0 * (1.0 - base.x) * (1.0 - blend.x),
        base.y < 0.5 ? 2.0 * base.y * blend.y : 1.0 - 2.0 * (1.0 - base.y) * (1.0 - blend.y),
        base.z < 0.5 ? 2.0 * base.z * blend.z : 1.0 - 2.0 * (1.0 - base.z) * (1.0 - blend.z));
    return opacity * r + (1.0 - opacity) * base;
}

vec3 blend_soft_light(vec3 base, vec3 blend, float opacity) {
    vec3 r = vec3(
        blend.x < 0.5 ? 2.0*base.x*blend.x + base.x*base.x*(1.0-2.0*blend.x) : 2.0*base.x*(1.0-blend.x) + sqrt(base.x)*(2.0*blend.x-1.0),
        blend.y < 0.5 ? 2.0*base.y*blend.y + base.y*base.y*(1.0-2.0*blend.y) : 2.0*base.y*(1.0-blend.y) + sqrt(base.y)*(2.0*blend.y-1.0),
        blend.z < 0.5 ? 2.0*base.z*blend.z + base.z*base.z*(1.0-2.0*blend.z) : 2.0*base.z*(1.0-blend.z) + sqrt(base.z)*(2.0*blend.z-1.0));
    return opacity * r + (1.0 - opacity) * base;
}

vec3 blend_additive(vec3 base, vec3 blend, float opacity) {
    return base + blend * opacity;
}

vec3 blend_difference(vec3 base, vec3 blend, float opacity) {
    return opacity * abs(base - blend) + (1.0 - opacity) * base;
}
```

Core fragment strategy:

```glsl
vec4 base = texture(TEXTURE, UV);
vec3 blend_col = texture(blend_texture, UV).rgb;
COLOR.rgb = blend_overlay(base.rgb, blend_col, blend_opacity);
COLOR.a = base.a;
```

Watch for:

- alpha channel corruption — blend modes operate on RGB only, preserve original alpha
- HDR overflow on additive blends — clamp output if the project doesn't use HDR 2D
- performance of branching blend mode selection — prefer separate shaders per mode over runtime `if` chains

Validation:

- compare output against a reference (Photoshop/GIMP with same blend mode)
- confirm alpha is unchanged
- test with `blend_opacity` at 0.0 (should be identity) and 1.0 (full effect)

### Procedural Noise Pattern

Use when:

- a dissolve, fire, smoke, or energy effect needs runtime noise without a baked texture
- the effect needs to animate smoothly via `TIME` without scrolling a texture
- memory budget is tight and a noise texture would be wasteful

Typical host:

- `Sprite2D` for entity effects (fire aura, energy shield shimmer)
- `ColorRect` for background effects or transitions
- any `CanvasItem` needing animated procedural variation

Core noise functions (include in shader):

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

float fbm(vec2 uv, vec2 size, int octaves, float persistence) {
    float value = 0.0, scale = 1.0, total = 0.0;
    for (int i = 0; i < octaves; i++) {
        value += value_noise(uv, size) * scale;
        total += scale;
        size *= 2.0;
        scale *= persistence;
    }
    return value / total;
}
```

Core fragment strategy (animated dissolve without texture):

```glsl
uniform float noise_scale : hint_range(1.0, 32.0) = 8.0;
instance uniform float dissolve_amount : hint_range(0.0, 1.0) = 0.0;
uniform float edge_width : hint_range(0.0, 0.2) = 0.05;
uniform vec4 edge_color : source_color = vec4(1.0, 0.5, 0.0, 1.0);

void fragment() {
    vec4 base = texture(TEXTURE, UV);
    float noise = fbm(UV + vec2(TIME * 0.1, 0.0), vec2(noise_scale), 4, 0.5);
    float edge = smoothstep(dissolve_amount, dissolve_amount + edge_width, noise);
    float visible = step(dissolve_amount, noise);
    COLOR.rgb = mix(edge_color.rgb, base.rgb, edge);
    COLOR.a = base.a * visible;
}
```

Watch for:

- performance on low-end devices — 4 octaves of FBM is ~40 ALU ops per pixel; reduce to 2-3 for mobile
- visible tiling at low `noise_scale` — increase scale or add a small TIME-based offset
- loop unrolling — use a fixed `const int` for octave count when possible for better GPU optimization

Validation:

- confirm noise animates smoothly without jumps
- test at multiple noise scales for visible repetition
- profile on target hardware if the effect covers large screen area

## Pattern Boundaries

- If the effect must read or rewrite the whole screen as a renderer-level post-process, escalate to `godot-architect`.
- If the main issue is asset padding, grayscale authoring, or LUT preparation, involve `godot-prototype-assets-2d`.
- If the work is mostly UI layout and only lightly uses a shader, let `godot-ui-core` own the layout and use this skill only for the shader slice.
- If the task is building UI shapes from SDF primitives without writing shader code (rounded buttons, pill tags, cards with cutouts), route to `godot-procedural-ui` — it uses an addon-driven resource system, not hand-written shaders.
- For the full procedural noise, SDF, and blend mode function library (all variants, 3D versions, cellular/voronoi), see `../../foundation/procedural-noise-and-sdf-library.md`.
