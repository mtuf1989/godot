# Material Catalog

Use this file first when choosing or reviewing a `spatial` shader material pattern.

## Table of Contents

- [Shared Rules](#shared-rules)
- [Renderer Baseline](#renderer-baseline)
- [Water](#water)
- [Terrain](#terrain)
- [Foliage](#foliage)
- [Skin](#skin)
- [Hair](#hair)
- [Eyes](#eyes)
- [Glass and Refraction](#glass-and-refraction)
- [Hologram and Sci-Fi](#hologram-and-sci-fi)
- [Pattern Boundaries](#pattern-boundaries)

## Shared Rules

- Stay inside the `spatial` shader boundary: 3D surface materials on `MeshInstance3D` nodes.
- Treat the code snippets below as core math sketches, not full drop-in shaders.
- Default to these implementation rules unless grounded project facts force a change:
  - `shader_type spatial;`
  - snake_case for uniforms, locals, and helper functions
  - `group_uniforms` for inspector organization
  - `source_color` on exposed colors, `hint_normal` on normal maps, `hint_default_white` / `hint_default_black` for optional textures
  - `instance uniform` for runtime values that vary per entity
  - explicit render modes: `blend_mix`, `depth_draw_opaque`, `cull_back` unless the pattern requires otherwise
  - branch-light math with `mix()`, `step()`, `smoothstep()`, and `clamp()`
- Start from `StandardMaterial3D` / `ORMMaterial3D` when possible. Convert to shader code only when custom behavior is needed. Godot documents that these materials cover most PBR controls and can be converted to shader code for customization.
- Treat these as integration hazards:
  - transparency pipeline (sorting, no shadows, no screen texture capture, no instancing in Forward+)
  - Forward+ vs Mobile feature availability (SSS, normal-roughness buffer, TAA are Forward+ only)
  - custom `DEPTH` writes (disable early-Z/Hi-Z)
  - triplanar cost (3× texture samples per lookup)

## Renderer Baseline

Lock the project to **Forward+** for the full spatial shader feature set. Forward+ exposes SSS, the normal-roughness screen texture, TAA, and the full CompositorEffect pipeline. Mobile uses the same RenderingDevice backend but with tighter budgets and missing features.

Key renderer facts:
- **Opaque path**: depth pre-pass, instancing, normal-roughness buffer participation, shadow casting. This is the fast default.
- **Alpha-tested path** (`alpha_scissor`, `alpha_hash`): still participates in depth pre-pass. Use for foliage, fences, grates. `alpha_hash` is better for hair-like coverage. Alpha antialiasing (`alpha_to_coverage`) works best with MSAA.
- **Transparent path** (`blend_mix`, `blend_add`, `blend_premul_alpha`): sorted by node position, no per-vertex sorting, no OIT. Cannot cast shadows. Does not appear in `hint_screen_texture` or `hint_depth_texture`. Excluded from automatic instancing in Forward+. Use only when genuinely needed (water, glass, hologram).
- **TAA**: recommended for photorealistic projects. Best defense against specular aliasing and shader shimmer. Forward+ only.
- **Screen-space roughness limiter**: enabled by default. Tames specular aliasing on pores, brushed metals, dense hair.
- **Roughness limiter on import**: uses normal map to reduce specular aliasing at zero runtime cost.

## Water

Use when: the project needs ocean, river, lake, puddle, or waterfall surfaces.

Render pipeline: **transparent** — water needs screen-space refraction and depth-based effects.

### Core Architecture

Split low-frequency waves into **vertex displacement** and high-frequency ripples into **scrolling normal maps**. This is the classic real-time water split: geometric undulation for broad form, normal-map waves for perceived realism.

### Gerstner Vertex Displacement

Gerstner waves sharpen crests by adding lateral displacement, not just vertical sine motion. Expose artist-facing controls: wave count, wavelength range, amplitude range, speed scalar, global steepness. Clamp steepness so the aggregate `Q_i * w_i * A_i` never reaches loop-forming values.

Production rules:
- Use **directional** Gerstner for oceans, lakes, wind-driven surfaces
- Use **circular** waves only for local emitters (waterfall pools, impact ripples)
- Keep geometric wave count modest (4–8 waves)
- Attenuate geometric amplitude to zero in shallow/shoreline regions using depth comparison or authored mask

```glsl
// Illustrative single Gerstner wave contribution
vec3 gerstner_wave(vec3 world_pos, vec2 direction, float wavelength, float steepness, float speed, float time_val) {
    float k = TAU / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(direction);
    float f = k * (dot(d, world_pos.xz) - c * speed * time_val);
    float a = steepness / k;
    return vec3(d.x * a * cos(f), a * sin(f), d.y * a * cos(f));
}
```

### Screen-Space Refraction

Read resolved screen color with `hint_screen_texture`. Derive UV offset from water normal. Use `textureLod()` with mipmap-enabled filter for roughness-driven blur. Clamp offset using scene depth to prevent refraction pulling background pixels across hard intersections.

```glsl
// Illustrative refraction pattern
uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform sampler2D depth_tex : hint_depth_texture, repeat_disable, filter_nearest;

// In fragment():
float scene_depth_raw = textureLod(depth_tex, SCREEN_UV, 0.0).r;
vec4 scene_vs4 = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene_depth_raw, 1.0);
vec3 scene_vs = scene_vs4.xyz / scene_vs4.w;
float thickness = max(0.0, scene_vs.z - VERTEX.z);

vec2 refract_uv = SCREEN_UV + normal_xy * refract_scale * clamp(abs(thickness) * depth_factor, 0.0, 1.0);
vec3 refracted = textureLod(screen_tex, refract_uv, roughness_lod).rgb;
```

### Depth-Based Foam

Use depth comparison as runtime proxy for "water meets geometry." Combine:
- `shore_mask` from background-depth minus water-depth
- `crest_mask` from wave steepness or Gerstner vertical derivative
- `noise_mask` from world-space foam texture
- Optional: background-normal contrast from `hint_normal_roughness_texture` (Forward+ only)

### Shoreline Attenuation

Attenuate geometric wave amplitude in shallow areas. Use depth comparison or authored shoreline mask. This prevents waves from clipping through terrain at banks.

## Terrain

Use when: the project needs ground surfaces with multi-texture splatting.

Render pipeline: **opaque** — terrain wants shadows, GI, normal-roughness buffer, cheapest overdraw.

### Texture2DArray Splatting

Use `Texture2DArray` for albedo, normals, and packed ORM. Each layer is separate, no padding needed, mipmaps per layer. Scales cleanly beyond 4 materials. Control maps store layer weights in RGBA channels.

```glsl
uniform sampler2DArray albedo_array;
uniform sampler2DArray normal_array;
uniform sampler2DArray orm_array;
uniform sampler2D control_map : filter_linear;

// In fragment():
vec4 weights = texture(control_map, UV);
weights /= max(dot(weights, vec4(1.0)), 0.001); // normalize

vec3 albedo = vec3(0.0);
for (int i = 0; i < 4; i++) {
    albedo += texture(albedo_array, vec3(UV * tiling[i], float(i))).rgb * weights[i];
}
```

### Slope-Gated Triplanar

Do not run triplanar for every layer on every pixel. Compute slope factor from world normal. Blend toward triplanar only on steep slopes and cliffs. Use world-space triplanar for terrain chunks that must align across boundaries.

Triplanar costs 3× texture samples per lookup. A naive 4-layer triplanar PBR terrain multiplies fetches rapidly. Slope-gating is the difference between a shippable terrain shader and an over-sampled one.

See `references/advanced-techniques.md` for the triplanar implementation pattern.

### Distance Blending

Combine a near sample (tight tiling for local detail) with a far sample (larger world-space scale for macro shape). Blend by camera distance. Optionally multiply by low-frequency macro variation noise. This hides tiling repetition on large terrain surfaces.

### Height Blending

When splatting layers, use heightmap-weighted blending instead of linear interpolation. This produces natural-looking transitions where rock pokes through dirt at high points rather than uniform gradients.

## Foliage

Use when: the project needs leaves, grass, bushes, or other vegetation.

Render pipeline: **alpha-tested** — use `alpha_scissor` for most foliage, `alpha_hash` for softer coverage. Pair with `alpha_to_coverage` and MSAA for edge quality. Avoid `blend_mix` transparency for bulk vegetation.

### Alpha Mode Hierarchy

1. **Alpha Scissor**: ideal for leaves, fronds, fences, grass. Hard cutout, participates in depth pre-pass, shadow casting, instancing.
2. **Alpha Hash**: better for hair-like coverage, soft edges. Dithered, no hard cutoff.
3. **Depth Pre-Pass** (`depth_prepass_alpha`): draws opaque parts through opaque pipeline, transparent remainder separately. Better sorting, shadow casting, but slower than pure alpha-test. Does NOT benefit from automatic instancing in Forward+.
4. **Alpha Blend**: only for VFX-style surfaces, not bulk foliage.

### Wind Animation

Keep wind in the vertex stage. Use vertex colors for masking:
- `COLOR.r`: trunk-to-tip bend weight
- `COLOR.g`: secondary flutter or leaf-edge motion
- `INSTANCE_ID` or `INSTANCE_CUSTOM`: phase-offset identical instances

```glsl
// Illustrative vertex wind
void vertex() {
    float phase = float(INSTANCE_ID) * 0.37;
    float bend = sin(TIME * wind_speed + VERTEX.x * 0.5 + phase) * wind_strength;
    VERTEX.x += bend * COLOR.r;
    VERTEX.z += bend * COLOR.r * 0.5;
}
```

### Translucency

Three viable tools, increasing cost:
1. **Back Lighting** (`BACKLIGHT`): cheapest. Good for most foliage. Explicitly designed for plant leaves and grass.
2. **Lambert Wrap** (`diffuse_lambert_wrap` render mode): extends diffuse beyond 90°. Cheap SSS approximation for grass cards.
3. **SSS Transmittance** (`SSS_STRENGTH`, `SSS_TRANSMITTANCE_*`): Forward+ only. For hero leaves, broad banana leaves, close-up plants where transmission color matters.

Rule: grass and distant foliage use Back Lighting. Midground leaves use Back Lighting + optional transmittance. Hero foliage uses SSS transmittance in Forward+.

### Culling and Performance

- `cull_disabled` for genuinely thin leaf cards that need two-sided lighting. Exception, not default.
- For dense grass or numerous shrubs, `MultiMesh` is the high-volume path. Godot describes it as suitable for "many thousands" of identical objects.
- Vertex lighting (`vertex_lighting` render mode) is a valid cheaper tier for extremely numerous shrubs.
- Distance fading: use **Pixel Dither** or **Object Dither** instead of Pixel Alpha — dithered modes remain effectively opaque and are cheaper.
- Split assets: if only a small section is transparent (tree trunk + leaf cards), separate into two surfaces so the opaque bulk stays fully opaque.

## Skin

Use when: the project needs character skin with subsurface scattering.

Render pipeline: **opaque**. SSS is Forward+ only.

### Texture Stack

A production skin material uses separate masks rather than a single SSS slider:
- **Albedo**: scan-quality or carefully painted, no baked lighting
- **Primary normal**: form-level shape
- **Micro/detail normal**: pores (use detail UV or secondary UV)
- **Roughness**: artist-painted, modulated by micro normal response
- **Specular mask**: tighten highlights on stretched, oily skin
- **Subsurface/scatter mask**: strongest on cheeks, nose, ears, lips
- **Transmission/thickness mask**: strongest where skin is thinnest (ear rims, nostrils, eyelids, lip edges)

### Shader Hooks

Godot exposes: `SSS_STRENGTH`, `SSS_TRANSMITTANCE_COLOR`, `SSS_TRANSMITTANCE_DEPTH`, `SSS_TRANSMITTANCE_BOOST`, `BACKLIGHT`. BaseMaterial3D has a `sss_mode_skin` that boosts the red channel for human skin.

```glsl
// Illustrative skin fragment
ALBEDO = texture(albedo_tex, UV).rgb;
NORMAL_MAP = texture(normal_tex, UV).rgb;
ROUGHNESS = texture(roughness_tex, UV).r * roughness_scale;
METALLIC = 0.0;
SPECULAR = texture(specular_tex, UV).r;
SSS_STRENGTH = texture(sss_tex, UV).r * sss_scale;
SSS_TRANSMITTANCE_COLOR = transmittance_color;
SSS_TRANSMITTANCE_DEPTH = texture(thickness_tex, UV).r * transmittance_depth;
```

### Detail Normals

Use Godot's detail texture system or a secondary UV channel for pore-level normals. Derive roughness compensation from the micro normal so animated wrinkle normals drive roughness changes consistently.

## Hair

Use when: the project needs hair cards or groom-like strips with strand-direction highlights.

Render pipeline: **alpha-tested** preferred. Use `alpha_hash` for realistic hair, `depth_prepass_alpha` when partial translucency is needed. Pair with MSAA + alpha antialiasing.

### Built-in Anisotropy

Godot exposes `ANISOTROPY` and `ANISOTROPY_FLOW` in spatial shaders, plus `anisotropy_flowmap` in BaseMaterial3D. The flowmap's RG channels offset tangent-space distortion, alpha modulates strength. Good for production-efficient hair cards.

### Dual-Lobe Strand Specular

For hero characters, write a custom `light()` for Kajiya-Kay-style dual-lobe response:
- Primary lobe: shifted toward root, tight exponent, neutral/white color
- Secondary lobe: shifted toward tip, wider exponent, tinted by hair color
- Noise texture breaks up the secondary lobe

```glsl
float strand_lobe(vec3 T, vec3 H, float exponent) {
    float th = clamp(dot(T, H), -1.0, 1.0);
    return pow(max(1.0 - th * th, 0.0), exponent * 0.5);
}

void light() {
    vec3 T = normalize(TANGENT);
    vec3 H = normalize(LIGHT + VIEW);
    float n = texture(shift_noise_tex, UV).r * 2.0 - 1.0;
    vec3 T1 = normalize(T + (primary_shift + n * 0.05) * NORMAL);
    vec3 T2 = normalize(T + (secondary_shift - n * 0.08) * NORMAL);
    float diff = max(0.0, 0.75 * dot(NORMAL, LIGHT) + 0.25);
    float spec1 = strand_lobe(T1, H, primary_exp);
    float spec2 = strand_lobe(T2, H, secondary_exp);
    DIFFUSE_LIGHT += ALBEDO * diff * ATTENUATION * LIGHT_COLOR / PI;
    SPECULAR_LIGHT += (vec3(1.0) * spec1 + ALBEDO * 0.6 * spec2) * ATTENUATION * LIGHT_COLOR * SPECULAR_AMOUNT;
}
```

Caution: custom `light()` will not run when vertex lighting is forced (mobile default).

### Transparency Strategy

- **Alpha Hash**: recommended by Godot for realistic hair. Dithered, no hard cutoff.
- **Depth Pre-Pass**: better sorting and shadow casting, but loses automatic instancing.
- Pair with MSAA (at least 2×) and alpha antialiasing for edge quality.

## Eyes

Use when: the project needs character eyes with iris depth and corneal refraction.

Render pipeline: **opaque** for the eyeball, optional transparent cornea shell.

### Two Viable Workflows

1. **Single-mesh eye shader**: fakes iris depth and corneal refraction in one shader. UV requirement: front half projected so iris center is at UV center, UV "up" matches model-space up. Uses custom iris POM for depth illusion.
2. **Multi-geometry eye**: sclera/iris eyeball + cornea bulge + tearline/lacrimal geometry + optional eyelid occlusion. More film-traditional, better socket integration.

### Iris Parallax

Use custom POM (see `references/advanced-techniques.md`) rather than Godot's generic height mapping for eyes. UV-centered iris parallax is more controllable. Keep the cornea much smoother (lower roughness) than sclera.

### Cornea Refraction

For single-mesh: use screen-space refraction or a manual Fresnel + distortion approach. For multi-geometry: the cornea shell can use `blend_mix` with low alpha and screen-texture refraction.

## Glass and Refraction

Use when: the project needs transparent glass, frosted glass, stained glass, or visor materials.

Render pipeline: **transparent** — refraction forces the transparent pipeline.

### Built-in Refraction

`StandardMaterial3D` supports refraction: fetches behind the material, uses material roughness to blur, refraction texture distorts per pixel. Limitations: screen-space only, cannot refract other transparent materials, cannot show off-screen objects, edge artifacts on opaque objects in front.

### Frosted Glass via Mipmap LOD

Skip built-in refraction. Read screen texture with `textureLod()` and mipmap-enabled filter. Larger LOD = blurrier. Map blur strength to roughness, noise, fingerprints, or mask detail.

```glsl
shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_back;

uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform sampler2D normal_tex;
uniform float refraction_scale = 0.015;
uniform float blur_strength = 5.0;

void fragment() {
    vec3 n = texture(normal_tex, UV).xyz * 2.0 - 1.0;
    vec2 refr_uv = SCREEN_UV + n.xy * refraction_scale;
    float lod = blur_strength * ROUGHNESS;
    vec3 behind = textureLod(screen_tex, refr_uv, lod).rgb;
    ALBEDO = vec3(0.95);
    ROUGHNESS = 0.8;
    METALLIC = 0.0;
    ALPHA = 0.12;
    EMISSION = behind * 0.85;
}
```

### SubViewport Fallback

For hero frosted glass, visors, cockpit canopies where screen texture limitations are unacceptable: render through a SubViewport with a camera in the same position, use that viewport texture instead. Stable blurred background without screen-texture single-capture limits.

## Hologram and Sci-Fi

Use when: the project needs hologram, glitch, scanning, cloaking, or edge-glow materials.

Render pipeline: **transparent** with `blend_add` for purely additive holograms, `blend_premul_alpha` for grounded translucent effects. Consider `alpha_hash` or dither distance-fade if sorting problems are worse than the dithered look.

### Core Layers

A reliable hologram combines four layers:
1. **Base emissive tint**: `EMISSION` with HDR values for bloom interaction
2. **Fresnel edge boost**: `pow(1.0 - max(dot(NORMAL, VIEW), 0.0), exponent)` — cheaper and more controllable than built-in `RIM`
3. **Animated scanlines/bands**: `fract(UV.y * density + TIME * speed)` with `smoothstep` thresholds
4. **Noise-driven UV distortion / alpha dropout**: `NoiseTexture2D` from `FastNoiseLite` for consistent breakup

```glsl
shader_type spatial;
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled;

uniform sampler2D noise_tex;
uniform vec3 hologram_colour : source_color = vec3(0.1, 0.8, 1.5);
uniform float line_density = 90.0;
uniform float flicker_speed = 14.0;

void fragment() {
    float fresnel = pow(1.0 - max(dot(normalize(NORMAL), normalize(VIEW)), 0.0), 3.0);
    float lines = smoothstep(0.45, 0.55, fract(UV.y * line_density + TIME * 3.0));
    float n = texture(noise_tex, UV * 3.0 + vec2(TIME * 0.2, -TIME * 0.4)).r;
    float glitch = step(0.88, texture(noise_tex, vec2(UV.y * 6.0, TIME * 0.6)).r);
    vec2 uv = UV + vec2(glitch * 0.03, 0.0);
    float breakup = smoothstep(0.15, 0.85, texture(noise_tex, uv * 4.0 + TIME * 0.15).r);
    ALBEDO = vec3(0.0);
    ALPHA = 0.08 + fresnel * 0.25 + breakup * 0.12;
    EMISSION = hologram_colour * (0.6 + lines * 0.8 + fresnel * 3.0) * (0.85 + 0.15 * sin(TIME * flicker_speed + n * 10.0));
}
```

### Material Overlay

For shield/hologram overlays on existing meshes, use Godot's `Material Overlay` property. It renders a transparent material over the existing one without rebuilding the base material. Ideal for additive hologram/shield passes.

### Noise Sources

Use `NoiseTexture2D` / `NoiseTexture3D` from `FastNoiseLite` as texture uniforms rather than hard-coding procedural noise. Supports mipmaps, seamless textures, color ramps, and 3D textures. Better production input for consistent glitch breakup across assets.

## Pattern Boundaries

- If the effect is VFX-motivated (energy shields as particle-driven, distortion planes for shockwaves, particle mesh shaders) → `godot-vfx`
- If the main issue is asset preparation (texture authoring, UV layout, mesh topology) → note the requirement and escalate
- If the work is mostly gameplay glue and only lightly uses a shader → `godot-gdscript`
- If architecture or renderer decisions are still open → `godot-architect`
- If the task involves raw compute shaders → `godot-compute`
