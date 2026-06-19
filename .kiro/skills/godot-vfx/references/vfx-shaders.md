# VFX Shaders Reference

Spatial shaders for 3D VFX meshes: vertex displacement, refraction, screen-reading, depth interaction, INSTANCE_CUSTOM bridging, cel-shading for VFX, SDF raymarching, and hologram/glitch patterns.

## Table of Contents

1. [INSTANCE_CUSTOM Data Bridging](#instance-custom)
2. [Unshaded vs Lit VFX Materials](#unshaded-vs-lit)
3. [Vertex Displacement](#vertex-displacement)
4. [Screen-Space Refraction and Distortion](#refraction)
5. [Depth-Buffer Interaction for VFX](#depth-interaction)
6. [PBR for Participating Media](#pbr-media)
7. [Cel-Shading for VFX Meshes](#cel-shading)
8. [SDF Raymarching](#raymarching)
9. [Hologram, Glitch, and Sci-Fi Energy Shaders](#hologram-glitch)
10. [Premultiplied Alpha Blending](#premultiplied-alpha)

---

## 1. INSTANCE_CUSTOM Data Bridging <a name="instance-custom"></a>

The critical workflow for per-particle visual variation. Particle simulation runs in a `shader_type particles` shader. Visual rendering runs in a `shader_type spatial` shader on the emitted mesh. Data flows between them via the CUSTOM/INSTANCE_CUSTOM channel.

### Writing (Particle Shader)

```glsl
shader_type particles;

void start() {
    CUSTOM.x = float(INDEX) / float(AMOUNT); // birth seed (0–1)
    CUSTOM.y = 0.0; // normalized age
    CUSTOM.z = 0.0; // reserved for collision data
    CUSTOM.w = 0.0; // reserved
}

void process() {
    CUSTOM.y = 1.0 - (LIFETIME - ELAPSED_TIME) / LIFETIME;
}
```

### Reading (Spatial Shader)

```glsl
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back;

uniform sampler2D dissolve_noise;
uniform float dissolve_edge_width : hint_range(0.0, 0.2) = 0.05;

void fragment() {
    float age = INSTANCE_CUSTOM.y;       // normalized age from particle shader
    float seed = INSTANCE_CUSTOM.x;      // per-particle random offset

    // Dissolve effect unique per particle
    float noise = texture(dissolve_noise, UV + seed).r;
    float dissolve_threshold = age;
    float edge = smoothstep(dissolve_threshold, dissolve_threshold + dissolve_edge_width, noise);

    ALBEDO = vec3(1.0, 0.5, 0.0); // fire color
    EMISSION = vec3(3.0, 1.0, 0.0) * (1.0 - edge); // HDR glow at dissolve edge
    ALPHA = edge;
    ALPHA_SCISSOR_THRESHOLD = 0.5;
}
```

### Common INSTANCE_CUSTOM Packing Conventions

| Channel | Common Use |
|---------|-----------|
| `.x` | Birth seed / random offset (0–1) |
| `.y` | Normalized age (0–1) |
| `.z` | Collision penetration depth or custom state |
| `.w` | Velocity magnitude or custom flag |

When using ParticleProcessMaterial instead of a custom particle shader, configure the Custom properties in the material to pass data to INSTANCE_CUSTOM.

---

## 2. Unshaded vs Lit VFX Materials <a name="unshaded-vs-lit"></a>

### Unshaded (Pure Energy)

For effects that represent pure energy — laser beams, plasma, magical auras, impact flashes — bypass the lighting pipeline entirely:

```glsl
shader_type spatial;
render_mode unshaded, blend_add, cull_disabled;

uniform vec4 energy_color : source_color = vec4(1.0, 0.5, 0.0, 1.0);
uniform float energy_intensity : hint_range(1.0, 20.0) = 5.0;

void fragment() {
    ALBEDO = energy_color.rgb * energy_intensity;
    ALPHA = energy_color.a;
}
```

Benefits:
- No lighting evaluation cost
- Exact color control — the color you set is the color on screen
- Punches through complex environmental lighting
- Ideal for stylized/anime effects

### Lit (Environmentally Anchored)

For effects that must feel grounded in the scene — smoke, dust, debris, solidified shields — allow lighting interaction:

```glsl
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back;

void fragment() {
    ALBEDO = texture(albedo_tex, UV).rgb;
    NORMAL_MAP = texture(normal_tex, UV).rgb;
    ROUGHNESS = 0.8;
    ALPHA = texture(albedo_tex, UV).a;
}
```

Lit particles interact with the Forward+ clustered lighting grid, receiving dynamic lights, casting shadows (when enabled), and sampling ambient/GI.

### Shadow Casting from Particles

- Enable `cast_shadow` on GPUParticles3D
- Ensure scene lights have shadow maps enabled
- Dense smoke can cast volumetric-looking shadows
- Performance cost: shadow map must render particle geometry from light's perspective
- Use sparingly — only on hero effects where shadow contribution is visible

---

## 3. Vertex Displacement <a name="vertex-displacement"></a>

Vertex shaders manipulate mesh geometry before rasterization. For VFX: pulsating shields, rippling surfaces, organic morphing.

```glsl
shader_type spatial;

uniform sampler2D displacement_noise : repeat_enable, filter_linear_mipmap;
uniform float displacement_strength : hint_range(0.0, 2.0) = 0.5;
uniform float scroll_speed : hint_range(0.0, 2.0) = 0.3;

void vertex() {
    float noise_val = texture(displacement_noise, UV + TIME * scroll_speed).r;
    VERTEX += NORMAL * (noise_val * displacement_strength);
}
```

When performing heavy vertex displacement, recalculate normals. The original imported normals are no longer accurate after displacement. For simple cases, use `NORMAL = normalize(NORMAL)` after displacement. For accurate results, compute the derivative of the displacement to reconstruct normals mathematically.

---

## 4. Screen-Space Refraction and Distortion <a name="refraction"></a>

Screen-reading shaders access the rendered frame buffer to create refraction, heat haze, and shockwave effects.

### Basic Distortion

```glsl
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled, unshaded;

uniform sampler2D screen_texture : hint_screen_texture, filter_linear_mipmap;
uniform sampler2D distortion_noise : repeat_enable, filter_linear_mipmap;
uniform float distortion_strength : hint_range(0.0, 0.1) = 0.02;
uniform float scroll_speed : hint_range(0.0, 2.0) = 0.5;

void fragment() {
    vec2 noise_sample = texture(distortion_noise, UV + vec2(TIME * scroll_speed)).rg;
    vec2 distortion = (noise_sample - 0.5) * 2.0 * distortion_strength;
    vec3 refracted = texture(screen_texture, SCREEN_UV + distortion).rgb;
    ALBEDO = refracted;
    ALPHA = 0.5; // blend with scene
}
```

### Mipmap-Based Screen Blur

When the screen texture is sampled with `filter_linear_mipmap`, Godot automatically computes a blurred mip chain. Sampling higher LOD levels with `textureLod()` produces progressively blurrier reads at zero extra pass cost. This is ideal for VFX that need variable-intensity blur: heat haze, frosted force fields, dream/daze effects, energy shield refraction with roughness variation.

```glsl
shader_type spatial;
render_mode blend_mix, cull_disabled, unshaded;

uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform sampler2D distortion_noise : repeat_enable, filter_linear_mipmap;
uniform float distortion_strength : hint_range(0.0, 0.1) = 0.02;
uniform float blur_lod : hint_range(0.0, 8.0) = 3.0;

void fragment() {
    vec2 noise_sample = texture(distortion_noise, UV + vec2(TIME * 0.3)).rg;
    vec2 distortion = (noise_sample - 0.5) * 2.0 * distortion_strength;

    // Higher LOD = blurrier. Map blur_lod to effect intensity, roughness, or age.
    vec3 refracted = textureLod(screen_texture, SCREEN_UV + distortion, blur_lod).rgb;
    ALBEDO = refracted;
    ALPHA = 0.6;
}
```

Combine with INSTANCE_CUSTOM to drive blur per-particle: a heat shimmer particle can increase blur LOD as it ages, or a shield can vary blur by surface roughness.

### Depth-Sorting Artifact

Screen-space distortion reads a 2D snapshot of the rendered world. Objects physically in front of the distortion plane can erroneously bleed into the refraction if the UV offset samples their pixels.

**Mitigation: Sub-Viewport isolation.** Render the background into a dedicated SubViewport with 2D HDR enabled and linear color space. The distortion shader samples this isolated texture instead of `hint_screen_texture`. Foreground elements, UI, and overlying fog remain immune to distortion.

Use Sub-Viewport isolation when:
- multiple overlapping distortion effects fire simultaneously
- foreground characters must never be distorted
- the effect is large enough that UV offsets reach foreground geometry

### Shockwave Distortion

A common VFX pattern: expanding ring of distortion from an impact point.

```glsl
shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled;

uniform sampler2D screen_texture : hint_screen_texture, filter_linear_mipmap;
uniform float ring_radius : hint_range(0.0, 1.0) = 0.0;
uniform float ring_width : hint_range(0.01, 0.2) = 0.05;
uniform float distortion_strength : hint_range(0.0, 0.1) = 0.03;

void fragment() {
    vec2 center = vec2(0.5);
    float dist = distance(UV, center);
    float ring = smoothstep(ring_radius - ring_width, ring_radius, dist)
               * (1.0 - smoothstep(ring_radius, ring_radius + ring_width, dist));

    vec2 dir = normalize(UV - center);
    vec2 distortion = dir * ring * distortion_strength;

    vec3 color = texture(screen_texture, SCREEN_UV + distortion).rgb;
    ALBEDO = color;
    ALPHA = ring * 0.8;
}
```

Drive `ring_radius` from 0→1 via Tween or AnimationPlayer to animate the expanding shockwave.

---

## 5. Depth-Buffer Interaction for VFX <a name="depth-interaction"></a>

Many VFX effects need to react to nearby scene geometry: energy shields that glow at contact points, force fields that crackle where they intersect terrain, shockwave distortion that respects foreground depth. All of these require reading the scene depth buffer and comparing it with the VFX fragment's own position.

### Depth Reconstruction Pattern

Godot provides `hint_depth_texture` for reading the scene depth buffer. The depth value is non-linear and must be reconstructed into view-space coordinates using `INV_PROJECTION_MATRIX`. Forward+ and Mobile use reverse-Z with depth in `[0, 1]`.

```glsl
shader_type spatial;
render_mode blend_add, cull_disabled, unshaded;

uniform sampler2D depth_texture : hint_depth_texture, repeat_disable, filter_nearest;

// Reconstruct view-space Z from the depth buffer at the current screen pixel.
float get_linear_depth(vec2 screen_uv, mat4 inv_proj) {
    float raw_depth = textureLod(depth_texture, screen_uv, 0.0).r;
    vec3 ndc = vec3(screen_uv * 2.0 - 1.0, raw_depth);
    vec4 view_pos = inv_proj * vec4(ndc, 1.0);
    view_pos.xyz /= view_pos.w;
    return -view_pos.z; // positive distance in front of camera
}
```

### Intersection Glow (Shield / Force Field Contact)

The most common VFX depth pattern: glow intensifies where the VFX mesh intersects scene geometry. Compare the scene depth with the fragment's own view-space depth to derive a proximity mask.

```glsl
uniform sampler2D depth_texture : hint_depth_texture, repeat_disable, filter_nearest;
uniform vec3 intersection_color : source_color = vec3(0.2, 0.8, 1.0);
uniform float intersection_width : hint_range(0.01, 2.0) = 0.3;
uniform float intersection_intensity : hint_range(1.0, 20.0) = 8.0;

void fragment() {
    float scene_depth = get_linear_depth(SCREEN_UV, INV_PROJECTION_MATRIX);
    float frag_depth = -VERTEX.z; // VERTEX is in view space in fragment()

    // How close the scene geometry is to this VFX fragment.
    float depth_diff = scene_depth - frag_depth;
    float intersection = 1.0 - smoothstep(0.0, intersection_width, depth_diff);

    // Base shield appearance (Fresnel edge + intersection glow).
    float fresnel = pow(1.0 - max(dot(NORMAL, VIEW), 0.0), 3.0);
    ALBEDO = vec3(0.0);
    EMISSION = intersection_color * (fresnel * 2.0 + intersection * intersection_intensity);
    ALPHA = fresnel * 0.15 + intersection * 0.8;
}
```

### Depth-Aware Distortion

Prevent shockwave distortion from bleeding through foreground objects by clamping the distortion offset when the scene depth is closer than the VFX fragment.

```glsl
uniform sampler2D screen_texture : hint_screen_texture, filter_linear_mipmap;
uniform sampler2D depth_texture : hint_depth_texture, repeat_disable, filter_nearest;

void fragment() {
    float scene_depth = get_linear_depth(SCREEN_UV, INV_PROJECTION_MATRIX);
    float frag_depth = -VERTEX.z;

    // Only distort pixels that are behind the VFX plane.
    float depth_mask = step(frag_depth, scene_depth);

    vec2 distortion = compute_distortion(UV); // your distortion logic
    vec2 final_uv = SCREEN_UV + distortion * depth_mask;
    ALBEDO = texture(screen_texture, final_uv).rgb;
}
```

### Common Depth-Based VFX Patterns

| Effect | Depth Usage |
|--------|-------------|
| Shield intersection glow | `smoothstep` on depth difference → emission boost |
| Soft particle edges | Fade alpha as fragment depth approaches scene depth |
| Shoreline foam/spray | Depth difference as proximity mask for foam texture |
| Scanning/pulse wave | Depth comparison to world-space distance from emitter |
| Depth-aware distortion | Mask distortion UV offset by depth comparison |

### Caveats

- The depth texture only contains opaque geometry. Transparent objects do not write depth, so VFX cannot intersect other transparent surfaces this way.
- Depth reconstruction uses `INV_PROJECTION_MATRIX`, which handles reverse-Z correctly on Forward+ and Mobile. Do not hard-code NDC assumptions.
- `VERTEX` in `fragment()` is already in view space — no extra transform needed for the fragment's own depth.

---

## 6. PBR for Participating Media <a name="pbr-media"></a>

Photorealistic VFX repurposes PBR texture maps to simulate volumetric behavior on flat billboard geometry.

| PBR Map | VFX Application |
|---------|----------------|
| **Albedo** | Single-scattering albedo: probability that light scatters vs absorbs. White for steam, dark gray for soot. |
| **Normal** | Pseudo-volumetric shape of the particle cloud. Allows flat billboards to receive directional lighting and fake internal self-shadowing. |
| **Roughness** | Optical sharpness of light interactions. Low roughness for refractive water droplets, high for diffuse smoke. |
| **Thickness/Density** | Optical depth for volumetric absorption. Controls how rapidly light attenuates through the particle center vs translucent edges. |

### Bloom Interaction

Particles with emission values > 1.0 trigger the HDR glow pipeline:
- The renderer isolates pixels exceeding the luminance threshold
- Multi-pass Gaussian blur creates the bloom halo
- Additive blend composites the result

For fire/plasma: push emission to 5.0–15.0 at the core, let periphery stay in 0.0–1.0 range. This creates an incandescent core with natural falloff.

### SSAO and SSR Limitations

Standard transparent particles do not write to the depth buffer, so they:
- Cannot receive SSAO darkening
- Cannot appear in screen-space reflections

If SSR or SSAO interaction is required, use alpha scissor (`ALPHA_SCISSOR_THRESHOLD`) to force depth buffer writes. Trade-off: hard pixel edges instead of soft transparency.

---

## 7. Cel-Shading for VFX Meshes <a name="cel-shading"></a>

For stylized VFX that must interact with scene lighting while maintaining hard-edged anime aesthetics.

### Basic Stepped Lighting

```glsl
shader_type spatial;

uniform vec4 base_color : source_color = vec4(0.8, 0.2, 0.1, 1.0);
uniform vec4 shadow_color : source_color = vec4(0.3, 0.05, 0.02, 1.0);
uniform float shadow_threshold : hint_range(0.0, 1.0) = 0.5;

void fragment() {
    ALBEDO = base_color.rgb;
}

void light() {
    float NdotL = dot(NORMAL, LIGHT);
    float cel = step(shadow_threshold, NdotL);
    vec3 lit_color = mix(shadow_color.rgb, base_color.rgb, cel);
    DIFFUSE_LIGHT += clamp(lit_color * LIGHT_COLOR * ATTENUATION, 0.0, 1.0);
}
```

### Three-Tone Model

Add a mid-tone band for richer shading:

```glsl
void light() {
    float NdotL = dot(NORMAL, LIGHT);
    float shadow = step(0.3, NdotL);
    float highlight = step(0.7, NdotL);
    vec3 color = shadow_color.rgb;
    color = mix(color, mid_color.rgb, shadow);
    color = mix(color, highlight_color.rgb, highlight);
    DIFFUSE_LIGHT += clamp(color * LIGHT_COLOR * ATTENUATION, 0.0, 1.0);
}
```

### Fresnel Rim Light

Fake emissive rim for silhouette readability:

```glsl
void fragment() {
    float fresnel = pow(1.0 - dot(NORMAL, VIEW), rim_thickness);
    EMISSION = rim_color.rgb * fresnel * rim_intensity;
}
```

### Performance: Vertex Lighting

For rapid, chaotic VFX that are on screen for fractions of a second, use `render_mode vertex_lighting` to evaluate lighting per-vertex instead of per-pixel. The precision loss is imperceptible on fast-moving particles but yields significant GPU savings.

---

## 8. SDF Raymarching <a name="raymarching"></a>

Signed Distance Fields can be raymarched in a fragment shader to render infinitely complex volumetric geometry inside a simple bounding mesh.

Use cases:
- nebulas, black holes, fractals
- organic morphological matter
- energy fields with boolean operations (union, smooth subtraction, intersection)

### Basic Raymarching Loop

```glsl
shader_type spatial;
render_mode unshaded, cull_front; // cull front so we see inside the bounding box

uniform int max_steps = 64;
uniform float max_distance = 10.0;
uniform float surface_threshold = 0.001;

float sdf_sphere(vec3 p, float radius) {
    return length(p) - radius;
}

void fragment() {
    vec3 ro = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
    vec3 rd = normalize((inverse(MODEL_MATRIX) * vec4(VERTEX, 1.0)).xyz - ro);

    float t = 0.0;
    for (int i = 0; i < max_steps; i++) {
        vec3 p = ro + rd * t;
        float d = sdf_sphere(p, 0.4);
        if (d < surface_threshold) {
            ALBEDO = vec3(1.0, 0.3, 0.0);
            ALPHA = 1.0;
            return;
        }
        t += d;
        if (t > max_distance) break;
    }
    discard;
}
```

### Boolean Operations

```glsl
float sdf_union(float a, float b) { return min(a, b); }
float sdf_subtract(float a, float b) { return max(a, -b); }
float sdf_intersect(float a, float b) { return max(a, b); }
float sdf_smooth_union(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
```

Smooth union allows two raymarched shapes to morph seamlessly into one — impossible with polygonal rendering.

### Environment SDF Interaction

Access the engine's global SDF texture within a spatial shader to make VFX react to nearby geometry. A force-field mesh can query its distance to the nearest wall and increase emission at intersection points, rendering an "energy crackle" where the shield contacts terrain.

---

## 9. Hologram, Glitch, and Sci-Fi Energy Shaders <a name="hologram-glitch"></a>

Holograms, scanning effects, cloaking shimmer, and glitch breakup are VFX mesh effects that combine Fresnel edge glow, animated scanlines, noise-driven distortion, and emissive flicker. These patterns apply to energy shields, sci-fi UI projections, scanning pulses, and cloaking/stealth effects.

### Core Layers

A reliable hologram/glitch material combines four layers:

| Layer | Purpose | Technique |
|-------|---------|-----------|
| Base emissive tint | Overall hologram color | `EMISSION = color * intensity` |
| Fresnel edge boost | Silhouette glow | `pow(1.0 - dot(NORMAL, VIEW), exponent)` |
| Animated scanlines | Horizontal band pattern | `fract(UV.y * density + TIME * speed)` |
| Noise distortion / dropout | Glitch breakup | Noise-driven UV offset + alpha cutout |

### Hologram Shader

```glsl
shader_type spatial;
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled;

uniform sampler2D noise_tex : repeat_enable, filter_linear_mipmap;
uniform vec3 hologram_color : source_color = vec3(0.1, 0.8, 1.5);
uniform float line_density : hint_range(10.0, 200.0) = 90.0;
uniform float flicker_speed : hint_range(1.0, 30.0) = 14.0;
uniform float glitch_threshold : hint_range(0.8, 1.0) = 0.88;

void fragment() {
    float fresnel = pow(1.0 - max(dot(normalize(NORMAL), normalize(VIEW)), 0.0), 3.0);
    float lines = smoothstep(0.45, 0.55, fract(UV.y * line_density + TIME * 3.0));
    float n = texture(noise_tex, UV * 3.0 + vec2(TIME * 0.2, -TIME * 0.4)).r;

    // Glitch: occasional horizontal UV shift driven by noise.
    float glitch = step(glitch_threshold, texture(noise_tex, vec2(UV.y * 6.0, TIME * 0.6)).r);
    vec2 uv = UV + vec2(glitch * 0.03, 0.0);
    float breakup = smoothstep(0.15, 0.85, texture(noise_tex, uv * 4.0 + TIME * 0.15).r);

    ALBEDO = vec3(0.0);
    ALPHA = 0.08 + fresnel * 0.25 + breakup * 0.12;
    EMISSION = hologram_color * (0.6 + lines * 0.8 + fresnel * 3.0)
             * (0.85 + 0.15 * sin(TIME * flicker_speed + n * 10.0));
}
```

### Blend Mode Selection

| Mode | Use Case |
|------|----------|
| `blend_add` | Pure energy holograms — additive glow, no occlusion |
| `blend_premul_alpha` | Grounded translucent holograms — partial occlusion with energy glow |
| Alpha Hash | Dithered cutout holograms — avoids transparency sorting entirely |

If the hologram can be mostly opaque with dithered cutouts, Alpha Hash or dither distance-fade modes are more robust than full alpha blending and avoid the transparent pipeline entirely.

### Noise Sources

Drive hologram shaders with `NoiseTexture2D` generated from `FastNoiseLite` rather than hard-coding procedural noise. Godot's noise textures support mipmaps, seamless tiling, and color ramps, making them a better production input for consistent glitch breakup across many assets.

### Material Overlay for Shields

For shield/hologram overlays on existing meshes, use Godot's Material Overlay (`GeometryInstance3D.material_overlay`). This renders a transparent material over the existing one without rebuilding the base material — ideal for adding a hologram/shield pass to any object.

---

## 10. Premultiplied Alpha Blending <a name="premultiplied-alpha"></a>

Introduced in Godot 4.3, premultiplied alpha pre-multiplies color by alpha before blending. This allows a single material to exhibit both additive (bright flames) and standard mix (dark smoke) blending simultaneously.

```glsl
shader_type spatial;
render_mode blend_premul_alpha, unshaded, cull_disabled;

void fragment() {
    vec4 tex = texture(particle_texture, UV);
    // Bright pixels blend additively, dark pixels blend normally
    ALBEDO = tex.rgb;
    ALPHA = tex.a;
}
```

Benefits:
- Halves particle count for mixed-energy thermal effects (fire + smoke in one system)
- Reduces overdraw by combining what previously required two separate particle systems
- No visible blend seam between additive and mix regions
