# Advanced Techniques

Use this file when the task involves parallax occlusion mapping, triplanar mapping, stencil buffer effects, custom depth writes, multi-pass rendering, or render priority management.

## Table of Contents

- [Parallax Occlusion Mapping](#parallax-occlusion-mapping)
- [Triplanar Mapping](#triplanar-mapping)
- [Stencil Buffer](#stencil-buffer)
- [Custom Depth Writing](#custom-depth-writing)
- [Multi-Pass Rendering](#multi-pass-rendering)
- [Render Priority and Transparency Sorting](#render-priority-and-transparency-sorting)

## Parallax Occlusion Mapping

Use when: brick walls, cobblestones, tiled floors, rocky surfaces, or any surface where depth cues matter and the viewer spends time close to it.

### Built-in vs Custom

Godot's `StandardMaterial3D` exposes `heightmap_deep_parallax` with `min_layers`/`max_layers` controls. Sufficient for quick inspector-driven heightmapped materials on UV-mapped opaque surfaces.

Custom shader is needed when:
- Self-shadowing is required
- Custom `DEPTH` writes for better depth-buffer interaction
- Adaptive step count beyond built-in inspector controls
- Distance-based LOD fade to flat normal mapping
- POM on surfaces that also use triplanar (built-in disables heightmap when triplanar is enabled)

### Core Ray March

Build a view-space TBN, transform `VIEW` into tangent space, step through the heightfield, refine the hit by interpolating between the last two samples.

Height convention: internally use `0.0 = surface top, 1.0 = deepest cavity`. Expose `invert_height` toggle for assets authored with the opposite convention. Godot's built-in exposes `heightmap_flip_texture` for the same reason.

```glsl
float sample_depth_map(vec2 uv) {
    float h = texture(height_tex, uv).r;
    if (invert_height) h = 1.0 - h;
    return 1.0 - h; // convert height to depth
}

vec3 pom_linear_refine(vec2 uv, vec3 view_dir_ts) {
    float ndotv = clamp(abs(view_dir_ts.z), 0.0, 1.0);
    float layer_count = mix(max_layers, min_layers, ndotv);
    float layer_depth = 1.0 / layer_count;
    vec2 parallax_offset = (view_dir_ts.xy / max(view_dir_ts.z, 0.001)) * height_scale;
    vec2 delta_uv = parallax_offset / layer_count;

    vec2 prev_uv = uv, cur_uv = uv;
    float prev_ray = 0.0, cur_ray = 0.0;
    float prev_map = sample_depth_map(prev_uv), cur_map = prev_map;

    for (int i = 0; i < 64; i++) {
        if (float(i) >= layer_count) break;
        prev_uv = cur_uv; prev_ray = cur_ray; prev_map = cur_map;
        cur_uv -= delta_uv;
        cur_ray += layer_depth;
        cur_map = sample_depth_map(cur_uv);
        if (cur_ray >= cur_map) {
            float after = cur_map - cur_ray;
            float before = prev_map - prev_ray;
            float w = clamp(before / (before - after), 0.0, 1.0);
            return vec3(mix(prev_uv, cur_uv, w), mix(prev_ray, cur_ray, w));
        }
    }
    return vec3(cur_uv, cur_ray);
}
```

### Adaptive Step Count

Fewer layers for near-normal incidence, more for grazing angles where aliasing is worst:
```glsl
float layer_count = mix(max_layers, min_layers, abs(view_dir_ts.z));
```
Typical range: 8–32 linear steps. 4–8 binary refinement steps if using relief-mapping-style binary search.

### Self-Shadowing

After finding the visible texel, march a second ray toward the light in tangent space. Accumulate partial occlusion for soft shadows.

```glsl
float pom_soft_shadow(vec2 hit_uv, float hit_depth, vec3 light_dir_ts, int steps, float softness) {
    if (light_dir_ts.z <= 0.001) return 0.0;
    float step_depth = hit_depth / float(steps);
    vec2 delta_uv = (light_dir_ts.xy / max(light_dir_ts.z, 0.001)) * height_scale / float(steps);
    vec2 ray_uv = hit_uv + delta_uv;
    float ray_depth = hit_depth - step_depth;
    float occlusion = 0.0;
    for (int i = 0; i < 32; i++) {
        if (i >= steps || ray_depth <= 0.0) break;
        float blocker = max(ray_depth - sample_depth_map(ray_uv), 0.0);
        float weight = 1.0 - (float(i) / float(steps - 1));
        occlusion += blocker * weight * softness;
        ray_uv += delta_uv;
        ray_depth -= step_depth;
    }
    return clamp(1.0 - occlusion, 0.0, 1.0);
}
```

Cost: doubles ray-march work per light. In `light()`, this runs per light per pixel. Use single dominant-light approximation in `fragment()` for most materials. Full per-light self-shadowing only for hero surfaces.

Caution: `light()` is disabled when vertex lighting is forced (mobile default).

### Distance LOD Fade

Fade POM to flat normal mapping beyond a distance threshold:
```glsl
float fade = smoothstep(lod_fade_begin, lod_fade_end, length(NODE_POSITION_VIEW));
vec2 final_uv = mix(pom_uv, UV, fade);
```

### POM UV Rule

POM modifies UV **before every other texture lookup**. Feed the displaced UV to albedo, normal map, roughness, metallic, AO — everything that should stick to the displaced surface.

### Heightmap Authoring

- Format: `R8` is sufficient for most POM. Higher precision only for large height ranges or smooth gradients.
- Convention: white = surface top, black = deepest cavity (most common). Expose flip toggle.
- Filtering: linear or anisotropic for realistic materials. Nearest only for intentionally pixelated look.
- Hints: do NOT use `source_color` on height textures — they are value textures, not color.

### Silhouette Limitation

POM does not add real geometry. The mesh silhouette remains flat. Strongest on broad surface patches viewed mostly front-on. Weakest on thin objects or prominent outer edges. No workaround without actual geometry (Godot has no tessellation).

## Triplanar Mapping

Use when: terrain chunks, cliff faces, or modular environment pieces need seamless texturing across mesh boundaries without UV seams.

### Cost

Godot samples a texture **three times per pixel** when triplanar is enabled — once per axis, blended by surface orientation. This is "much slower" than ordinary UV texturing (Godot docs).

### Slope-Gated Triplanar

Do not run triplanar everywhere. Compute slope from world normal, blend toward triplanar only on steep slopes:

```glsl
vec3 world_normal = (INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
float slope = 1.0 - abs(world_normal.y);
float triplanar_blend = smoothstep(slope_threshold, slope_threshold + slope_fade, slope);
// Use regular UV on flat ground, triplanar on steep slopes
vec3 color = mix(texture(albedo_tex, UV).rgb, triplanar_sample(world_pos), triplanar_blend);
```

### World-Space Triplanar

Use world-space coordinates for terrain chunks that must align across boundaries:
```glsl
vec3 triplanar_sample(sampler2D tex, vec3 world_pos, vec3 world_normal, float tiling) {
    vec3 blend = abs(world_normal);
    blend = pow(blend, vec3(4.0)); // sharpen blend
    blend /= dot(blend, vec3(1.0));
    vec3 x = texture(tex, world_pos.yz * tiling).rgb;
    vec3 y = texture(tex, world_pos.xz * tiling).rgb;
    vec3 z = texture(tex, world_pos.xy * tiling).rgb;
    return x * blend.x + y * blend.y + z * blend.z;
}
```

### Triplanar + Height Exclusion

Godot's `StandardMaterial3D` disables heightmap when triplanar is enabled. Custom shader can work around this, but triplanar POM means separate projection-space marches per axis — much more expensive. Practical choice: full POM on UV-mapped hero surfaces, ordinary triplanar normal mapping on large terrain.

## Stencil Buffer

Use when: the project needs object outlines, X-ray silhouettes, or custom masking.

### Status in Godot 4

Since Godot 4.5, `StandardMaterial3D` supports stencil buffer for outlines and X-ray. The spatial shader API marks stencil as **experimental**. Stencil reads are only supported in the **transparent pass**. The main renderer stencil cannot be copied to a custom compositor texture.

### Built-in Presets

- **Outline**: `StandardMaterial3D` configures `next_pass` stencil material automatically
- **X-Ray**: shows material only when occluded
- **Custom**: choose reference value and comparison operator (`compare_equal`, `compare_not_equal`, `compare_less`, `compare_greater_or_equal`)

### Depth-Test Inversion

For through-wall-only effects, combine stencil with `depth_test_inverted`. Godot documents this as useful for stencil effects — shows material only when occluded.

### Practical Limits

- Stencil reads only in transparent pass — materials that write stencil are drawn in transparent pass, inheriting transparency limitations
- Outline presets require meshes with connected faces and shared vertices — flat-shaded or disconnected shells show gaps
- Cannot copy stencil to custom compositor texture — limits stencil-compositor pipelines
- For camera-wide outlines, prefer Sobel depth/normal in compositor (see `references/compositor-effects.md`)

### Decision Tree

- **Interactable highlights**: built-in Outline stencil
- **See-through-walls silhouettes**: built-in X-Ray stencil
- **Screen-space edge detection**: depth reconstruction from `hint_depth_texture`, not stencil
- **Camera-wide NPR outlines**: compositor Sobel, not stencil
- **Custom object masking**: Custom stencil with chosen reference value

## Custom Depth Writing

Use when: POM surfaces, impostor geometry, or special effects need corrected depth-buffer interaction.

### Godot API

`DEPTH` is a writable fragment output in spatial shaders. Range `[0.0, 1.0]`. If written in any branch, must be written in every branch.

### Cost

Writing `DEPTH` disables early-Z and Hi-Z optimizations. The GPU cannot perform early fragment depth rejection when the fragment shader modifies depth. Reserve for surfaces where the benefit is visually obvious.

### Pattern

```glsl
// Illustrative: displace apparent depth along view ray
vec3 displaced_view_pos = VERTEX - normalize(VIEW) * (hit_depth * depth_units);
vec4 clip = PROJECTION_MATRIX * vec4(displaced_view_pos, 1.0);
DEPTH = clip.z / clip.w;
```

### Reverse-Z

Forward+ and Mobile use reverse-Z with Vulkan-style `z` in `[0, 1]`. Compatibility uses OpenGL-style NDC. Use `PROJECTION_MATRIX` and `INV_PROJECTION_MATRIX` rather than hard-coding depth conventions.

## Multi-Pass Rendering

Use when: the material needs multiple rendering passes on the same mesh.

### Material Overlay

`GeometryInstance3D.material_overlay` renders a transparent material over the existing one. Ideal for:
- Hologram/shield overlays without rebuilding the base material
- Additive glow passes
- Temporary effect layers

### Next Pass

`Material.next_pass` chains a second material after the first. Used for:
- Stencil-driven outline passes
- X-ray silhouette passes
- Grow-style outlines (expand mesh along normals in vertex shader, cull front faces)

### Render Order

Within the same pass type:
1. Opaque materials render first (front-to-back for early-Z efficiency)
2. Transparent materials render after (back-to-front for correct blending)
3. `render_priority` adjusts order within the transparent pass

### When to Use Each

- **Material Overlay**: additive/overlay effects that should not affect the base material's render state
- **Next Pass**: effects that need their own render mode, stencil config, or cull direction
- **Separate MeshInstance3D**: when the overlay needs different geometry (e.g., slightly scaled-up shell for outlines)

## Render Priority and Transparency Sorting

Use when: transparent materials overlap and need controlled draw order.

### How Godot Sorts

- Opaque: front-to-back (automatic, no user control needed)
- Transparent: sorted by **node position** (origin), not per-vertex. No per-pixel OIT.
- `render_priority`: integer that adjusts order within the transparent pass. Higher values render later (on top). Range: -128 to 127.

### Common Problems

- **Water under glass**: both transparent, sorted by node origin. If water node origin is behind glass node origin, water renders first (correct). If origins are close or swapped, artifacts appear.
- **Overlapping transparent foliage**: alpha-tested foliage avoids this entirely. Use `alpha_scissor` or `alpha_hash` instead of `blend_mix`.
- **Hologram on transparent surface**: use `Material Overlay` or `render_priority` to ensure correct layering.

### Rules

- Minimize transparent materials. Use opaque or alpha-tested whenever possible.
- When transparency is unavoidable, use `render_priority` to enforce intended layering.
- Do not rely on `render_priority` as a substitute for correct depth/transparency design.
- `depth_draw_always` on transparent materials can help with some sorting issues but has its own artifacts.
- For complex transparent layering (multiple overlapping glass panes), consider SubViewport-based compositing.
