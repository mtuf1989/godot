# Compositor Effects

Use this file first when building or reviewing a `CompositorEffect`-based post-processing pass.

## Table of Contents

- [Architecture](#architecture)
- [Bloom](#bloom)
- [Depth of Field](#depth-of-field)
- [Motion Blur](#motion-blur)
- [Global Outlines](#global-outlines)
- [Custom Tonemap and Color Grading](#custom-tonemap-and-color-grading)
- [Shared Production Rules](#shared-production-rules)
- [Pattern Boundaries](#pattern-boundaries)

## Architecture

### CompositorEffect Basics

`CompositorEffect` is a viewport-level hook for custom 3D rendering passes. It is **experimental** in Godot 4. Supported on **Forward+** and **Mobile**, not Compatibility.

Key properties set in `_init()`:
- `effect_callback_type`: pipeline stage (`PRE_OPAQUE`, `POST_OPAQUE`, `POST_SKY`, `PRE_TRANSPARENT`, `POST_TRANSPARENT`)
- `access_resolved_color`: triggers MSAA color resolve before your effect
- `access_resolved_depth`: triggers MSAA depth resolve
- `needs_motion_vectors`: enables velocity buffer generation during opaque pass
- `needs_normal_roughness`: enables normal/roughness buffer access (**Forward+ only**)

### Pipeline Stages

| Stage | Runs After | Runs Before | Use For |
|---|---|---|---|
| `PRE_OPAQUE` | Depth prepass | Opaque rendering | Custom depth effects |
| `POST_OPAQUE` | Opaque rendering | Sky | Pre-sky effects |
| `POST_SKY` | Sky | Back buffers, SSS, SSR | Pre-backbuffer effects |
| `PRE_TRANSPARENT` | Sky, back buffers | Transparent rendering | Pre-transparent effects |
| `POST_TRANSPARENT` | Transparent rendering | Built-in post, output | **Default for most post-processing** |

### Buffer Access

In `_render_callback()`:
```gdscript
var buffers: RenderSceneBuffersRD = render_data.get_render_scene_buffers()
var scene_data: RenderSceneData = render_data.get_render_scene_data()

var color_rid := buffers.get_texture("render_buffers", "color")
var depth_rid := buffers.get_texture("render_buffers", "depth")
var velocity_rid := buffers.get_velocity_texture()
var normal_rid := buffers.get_texture("forward_clustered", "normal_roughness") # Forward+ only
var cam_proj: Projection := scene_data.get_cam_projection()
```

`render_data` is only valid during the callback. Do not store it.

### Custom Working Textures

Use `RenderSceneBuffersRD.create_texture()` with a custom context name. Textures are cached by `(context, name)` — request the same chain every frame and the buffer manager returns the existing RID if viewport state hasn't changed.

```gdscript
const CTX := &"my_effect"
const TEX_A := &"work_a"

rsb.create_texture(CTX, TEX_A,
    RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT,
    RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_STORAGE_BIT,
    RenderingDevice.TEXTURE_SAMPLES_1,
    half_size, view_count, mip_count, false, true)

var slice: RID = rsb.get_texture_slice(CTX, TEX_A, view, mip, 1, 1)
```

### Compute vs Fragment

Godot's compositor tutorial uses compute. For image-to-image post passes, compute is the lower-friction path: `compute_list_begin()`, `compute_list_bind_compute_pipeline()`, `compute_list_dispatch()`. Fragment/raster is viable via `framebuffer_create()` + `draw_list_begin()` but requires more setup.

For neighbor-sampling effects (blur, edge detection), **read from one texture and write to another** — never read and write the same image in one pass.

### Shader Warm-Up

- Use **file-backed `.glsl` shaders**, not runtime string assembly. Runtime strings prevent precompilation and cache warming.
- Keep the pass graph fixed and predictable. A small set of stable pipeline variants.
- Since Godot 4.4: ubershaders and pipeline precompilation for Forward+/Mobile.
- Since Godot 4.5: shader baker for export-time baking.
- Feature-priming: instantiate hidden/off-screen versions of effects early so pipelines are precompiled before gameplay.

### Persistent Stack Design

Events do not create post effects; events feed data into post effects. A hit event adjusts chromatic-aberration weight. A detection event changes outline intensity. The compositor stack remains persistent, warm, and renderer-owned.

Assign the `Compositor` resource to `WorldEnvironment` for global effects. Use camera-level compositor overrides only for exceptional cameras (scopes, CCTV, photo mode).

## Bloom

Use when: the project needs custom bloom beyond Godot's built-in `Environment.glow_*`.

### When Built-in Glow Is Sufficient

Godot's built-in glow uses a mip-chain workflow with per-level weights, threshold, scale, normalization, intensity, blend mode, and glow map (lens dirt). The mobile path uses dual filtering (attributed to Marius Bjørge). Sufficient for most projects that don't need custom kernels, firefly suppression, or per-mip tinting.

### When Custom Bloom Is Needed

- Dual Kawase or custom 13-tap downsample kernel
- First-pass firefly suppression (Karis average)
- Per-mip color tinting for chromatic bloom
- Quadratic soft-knee threshold (Jimenez-style) instead of built-in threshold/scale
- Custom lens dirt modulation

### Pipeline

Stage: `POST_TRANSPARENT`. Operates on HDR color before tonemapping.

1. **Prefilter/Threshold**: soft-knee quadratic curve. Clamp extreme values for firefly suppression.
2. **Progressive Downsample**: 13-tap pattern through mip chain (5–8 levels). First downsample optionally uses Karis-weighted averaging to suppress fireflies.
3. **Progressive Upsample**: 9-tap tent filter walking back up the chain. Each level accumulates the blurred smaller mip.
4. **Composite**: add bloom to base HDR color. Dirt modulates bloom contribution, not base image.

Format: `RGBA16F` for HDR intermediates. First bloom mip at half resolution.

### Key Rules

- Bloom operates in HDR **before** tonemapping. Godot's own renderer computes glow before the tonemapper.
- Disable built-in `Environment.glow_enabled` when custom bloom is active.
- Energy conservation: downsample weights sum to 1.0, upsample tent is normalized, inter-mip contributions normalized.
- Firefly suppression on first downsample only (Karis average: weight by inverse luminance).

### Illustrative Soft-Knee Threshold

```glsl
vec3 apply_soft_knee(vec3 color, float threshold, float knee, float clamp_max) {
    color = min(color, vec3(clamp_max));
    float br = max(color.r, max(color.g, color.b));
    float curve_x = threshold - knee;
    float curve_y = 2.0 * knee;
    float curve_z = 0.25 / knee;
    float rq = clamp(br - curve_x, 0.0, curve_y);
    rq = curve_z * rq * rq;
    float contrib = max(rq, br - threshold) / max(br, 1e-5);
    return color * contrib;
}
```

## Depth of Field

Use when: the project needs custom DOF beyond Godot's built-in camera attribute DOF.

### When Built-in DOF Is Sufficient

Godot exposes DOF through `CameraAttributesPractical` (distance/transition controls) and `CameraAttributesPhysical` (focal length, aperture, focus distance). Built-in supports BOX, HEXAGON, CIRCLE bokeh presets and VERY_LOW through HIGH quality tiers. Sufficient for gameplay focus guidance and modest blur.

### When Custom DOF Is Needed

- Signed CoC authoring with per-shot art direction
- Barré-Brisebois hexagonal or Garcia circular separable bokeh
- Explicit near/far separation with premultiplied alpha compositing
- Half-resolution processing with custom reconstruction
- Tighter depth-discontinuity handling

### Pipeline

Stage: `POST_TRANSPARENT`. Reads resolved color and depth.

1. **CoC Generation**: reconstruct linear depth from depth buffer using `INV_PROJECTION_MATRIX`. Compute signed CoC (positive = far, negative = near) from thin-lens formula. Output to `R16F` texture.
2. **Near/Far Separation**: separate into premultiplied working buffers at half resolution. Near field needs tile-max CoC dilation for correct bleed.
3. **Blur**: separable blur weighted by CoC. Far and near blurred independently. Hex blur (Barré-Brisebois 2-pass rhomboid) or CoC-weighted Gaussian.
4. **Composite**: far under sharp, near over sharp. This ordering is physically correct and prevents background bleeding over foreground.

### Depth Reconstruction

Same pattern as Godot's screen-reading tutorial:
```glsl
float depth01 = texture(u_depth, uv).r;
float z_ndc = depth01 * 2.0 - 1.0;
vec4 ndc = vec4(uv * 2.0 - 1.0, z_ndc, 1.0);
vec4 view = inv_projection * ndc;
view /= view.w;
float linear_z = -view.z; // positive distance in front of camera
```

### CoC Formula

```glsl
float coc_sensor = abs((f * f * (z - zf)) / (N * z * (zf - f)));
float coc_px = 0.5 * coc_sensor * (render_width / sensor_width);
if (z < zf) coc_px = -coc_px; // negative = near
```

### Key Rules

- Disable built-in DOF on camera attributes when custom DOF is active.
- Half-resolution is the production default. Full-res only for unusually small radii or photo mode.
- Near-field alpha compositing prevents sharp cutouts at foreground silhouettes.
- TAA before DOF is the safest default unless you implement DOF-specific temporal logic.

## Motion Blur

Use when: the project needs per-pixel velocity-based motion blur.

### Pipeline

Stage: `POST_TRANSPARENT`. Reads resolved color, depth, and velocity buffer.

1. **Tile Max**: reduce velocity buffer to tile grid (e.g., 32×32 tiles). Each tile stores the largest-magnitude velocity vector.
2. **Neighbor Max**: 3×3 max of surrounding tiles. Dilates blur influence so fast objects affect adjacent tiles.
3. **Reconstruction/Gather**: for each pixel, sample along the tile-neighbor max velocity direction. Weight samples by soft depth comparison and velocity similarity. Jitter sample positions to reduce banding.

### Velocity Buffer

- Enabled via `needs_motion_vectors = true`
- Retrieved via `buffers.get_velocity_texture()`
- Generated during **opaque pass** — transparent objects do NOT contribute velocity data
- RG channels store screen-space displacement (signed, normalized)

### Key Rules

- Always clamp max velocity magnitude to prevent extreme blur (performance and artistic).
- Camera vs object motion: velocity buffer captures both. Separate or scale camera contribution for artistic control.
- Tile size 20–40 pixels is typical. Larger = fewer dispatches but coarser approximation.
- Sample count 8–16 taps per pixel is typical for moderate blur.
- Motion blur typically runs before DOF (Guertin et al. suggest slightly better results).
- Transparent objects have no velocity data — accept this limitation.

### Illustrative Soft Depth Weight

```glsl
float soft_depth_weight(float d_center, float d_sample) {
    float diff = d_sample - d_center;
    if (diff > 0.0) return exp(-diff * diff * 100.0);
    return 1.0;
}
```

## Global Outlines

Use when: the project needs camera-wide outlines independent of per-object setup.

### Sobel Depth/Normal (Compositor)

The better fit for the compositor architecture. True screen-space, viewport-wide. Scales with screen resolution, not scene node count. Decoupled from mesh authoring.

Stage: `POST_TRANSPARENT`. Reads resolved depth and optionally normal/roughness (Forward+ only).

Pipeline:
1. Sample depth (and normals if available) in a Sobel or Roberts cross kernel
2. Compute edge strength from depth discontinuities and normal discontinuities
3. Composite outline color over the scene

Normal/roughness greatly improves quality: depth alone misses internal edges and low-parallax contacts. On Mobile (no normal buffer), depth-only is the fallback.

### Inverted Hull (Material)

Object-space technique: render mesh a second time, expand along normals, cull front faces, draw backfaces as outline. Per-object, requires authoring. Best for selective character/stylized asset outlines, not camera-wide post.

Use `Next Pass` with a dedicated outline material. Works best on smooth, rounded meshes. Hard-edged models show seams unless smoothed normals are prepared.

### Decision

- **Camera-wide outlines**: Sobel depth/normal in compositor
- **Selective hero outlines**: inverted hull via Next Pass or stencil
- **Both**: compositor for global, material for hero emphasis

## Custom Tonemap and Color Grading

Use when: the project needs tonemap operators or color grading beyond Godot's built-in options.

### When Built-in Is Sufficient

Godot's `Environment` exposes tonemap mode (Linear, Reinhard, Filmic, ACES), white reference, exposure, and color correction via `ColorCorrection` resource with a 3D LUT. Sufficient for most projects.

### When Custom Is Needed

- Custom filmic curve with artist-tunable shoulder/toe/midtones
- Per-channel color grading not expressible through the built-in LUT
- Stylized tonemap (e.g., posterization, color banding for NPR)
- Tonemap that must interact with custom bloom (e.g., bloom-aware exposure)

### Pipeline

Stage: `POST_TRANSPARENT`. Operates on HDR color. If custom bloom is also active, tonemap runs **after** bloom composite.

The simplest compositor effect: read resolved color, apply math, write back. No intermediate textures needed.

```glsl
// Illustrative ACES approximation (Narkowicz fit)
vec3 aces_tonemap(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

### Key Rules

- Disable built-in tonemap when custom is active (set tonemap mode to Linear).
- Tonemap after bloom, before output.
- LUT-based grading: use a 3D LUT texture sampled with the tonemapped color as coordinates.
- Ordering in the compositor stack: bloom → tonemap → final output.

## Shared Production Rules

### Forward+ vs Mobile

| Feature | Forward+ | Mobile |
|---|---|---|
| CompositorEffect | ✅ | ✅ |
| `needs_normal_roughness` | ✅ | ❌ |
| `needs_motion_vectors` | ✅ | ✅ |
| TAA | ✅ | ❌ |
| SSS | ✅ | ❌ |
| Separate specular | ✅ | ❌ |

Design a two-tier strategy: Forward+ as premium authoring target (normal-assisted outlines, richer SSAO, generous kernels). Mobile as degradation path (depth-only outlines, smaller kernels, fewer passes).

### Effect Ordering

Typical compositor stack order:
1. Custom SSAO (if any) — `POST_OPAQUE` or `POST_SKY`
2. Custom bloom — `POST_TRANSPARENT`
3. Custom motion blur — `POST_TRANSPARENT`
4. Custom DOF — `POST_TRANSPARENT`
5. Custom outlines — `POST_TRANSPARENT`
6. Custom tonemap/color grading — `POST_TRANSPARENT`

Multiple effects at the same stage execute in the order they appear in the `Compositor.compositor_effects` array.

### Disable Conflicting Built-ins

When a custom compositor effect replaces a built-in:
- Custom bloom → disable `Environment.glow_enabled`
- Custom DOF → disable `CameraAttributes` DOF (set `dof_blur_far_enabled = false`, `dof_blur_near_enabled = false`)
- Custom tonemap → set `Environment.tonemap_mode` to Linear
- Custom motion blur → no built-in to disable (Godot has no built-in motion blur)

### Texture Formats

| Use | Format |
|---|---|
| HDR color intermediates | `DATA_FORMAT_R16G16B16A16_SFLOAT` |
| Signed CoC map | `DATA_FORMAT_R16_SFLOAT` |
| Velocity / tile max | `DATA_FORMAT_R16G16_SFLOAT` |
| Edge/outline mask | `DATA_FORMAT_R8_UNORM` |

### Known Caveats

- `CompositorEffect` is **experimental** — API may shift across Godot 4.x releases.
- Compositor callbacks may stop being invoked when the scene is not rendering a new frame. Test idle-scene behavior for time-based effects.
- GDExtension `_render_callback` dispatch had issues before Godot 4.4. Use 4.4+ for native code compositor effects.
- Mobile compositor compute paths may fail with storage-usage errors in some Godot versions. Validate early on target renderer.

## Pattern Boundaries

- If the post-processing is VFX-motivated (impact frames, speed lines, shockwave distortion during gameplay events) → `godot-vfx`
- If the task is WorldEnvironment/Environment configuration (adjusting built-in tonemap, SSAO, SSR, SDFGI settings) without custom shader work → `godot-architect`
- If the task requires raw compute shaders for simulation (not post-processing) → `godot-compute`
