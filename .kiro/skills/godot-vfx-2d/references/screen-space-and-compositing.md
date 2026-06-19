# Screen-Space VFX and Compositing Reference

Screen-reading shaders, BackBufferCopy, CanvasGroup compositing, SubViewport isolation, full-screen post-processing, multi-pass stacking, and HDR 2D for Godot 4.

## Table of Contents

1. [Screen Reading: hint_screen_texture](#screen-reading)
2. [BackBufferCopy](#back-buffer-copy)
3. [CanvasGroup Compositing](#canvas-group)
4. [SubViewport Isolation](#sub-viewport)
5. [Full-Screen Post-Processing](#full-screen-post)
6. [Multi-Pass Stacking](#multi-pass)
7. [HDR 2D](#hdr-2d)
8. [Production Scene Hierarchy](#production-hierarchy)

---

## 1. Screen Reading: hint_screen_texture <a name="screen-reading"></a>

### Modern API

In Godot 4, the old `SCREEN_TEXTURE` built-in is gone. Declare a `sampler2D` uniform with the `hint_screen_texture` hint and sample with `SCREEN_UV`:

```glsl
uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_nearest;
```

### Copy Behavior

In 2D, the **first** node that uses `hint_screen_texture` triggers a full-screen copy to a back buffer. Later screen-reading nodes do **not** trigger another copy — they all read from the same frozen source. This means:
- Overlapping screen-space effects do not automatically see each other's output
- Two overlapping distortions can be made intentionally non-compounding by relying on the default frozen source
- Truly compounding distortions need an explicit `BackBufferCopy` between them or a single combined pass

### Key Built-ins

- `SCREEN_UV` — normalized coordinate for the current screen pixel
- `SCREEN_PIXEL_SIZE` — inverse of current resolution, keeps offsets resolution-independent

### Multiple Samplers with Different Filters

Godot allows multiple uniforms pointing at the same screen texture hint with different sampler flags. Useful for combining sharp and blurred reads in one shader:

```glsl
uniform sampler2D screen_sharp : hint_screen_texture, repeat_disable, filter_nearest;
uniform sampler2D screen_soft  : hint_screen_texture, repeat_disable, filter_nearest_mipmap;
```

Use `textureLod()` with the mipmapped sampler for cheap blurred reads. Mipmapped screen reads only become meaningfully blurred if the filter hint includes mipmaps and you increase the LOD level.

### Practical Local Distortion Shader

```glsl
shader_type canvas_item;
render_mode unshaded;

uniform sampler2D screen_sharp : hint_screen_texture, repeat_disable, filter_nearest;
uniform sampler2D screen_soft  : hint_screen_texture, repeat_disable, filter_nearest_mipmap;

uniform vec2 centre_uv = vec2(0.5, 0.5);
uniform float radius = 0.14;
uniform float width = 0.03;
uniform float strength = 0.02;
uniform float blur_lod = 1.5;
uniform float chroma_px = 2.0;

void fragment() {
    vec2 delta = SCREEN_UV - centre_uv;
    float dist = length(delta);
    vec2 dir = dist > 0.0001 ? delta / dist : vec2(0.0);

    float ring = smoothstep(radius + width, radius, dist)
               * smoothstep(radius - width, radius, dist);

    vec2 refract_offset = dir * ring * strength;
    vec2 chroma_offset = dir * SCREEN_PIXEL_SIZE * chroma_px;

    vec3 colour;
    colour.r = textureLod(screen_sharp, SCREEN_UV + refract_offset + chroma_offset, 0.0).r;
    colour.g = textureLod(screen_soft,  SCREEN_UV + refract_offset, blur_lod).g;
    colour.b = textureLod(screen_sharp, SCREEN_UV + refract_offset - chroma_offset, 0.0).b;

    COLOR = vec4(colour, ring);
}
```

When paired with `BackBufferCopy`, the copied rectangle must be larger than the visible effect disc — expand by maximum UV displacement plus chromatic fringe width. Sampling outside the copied region produces undefined results.

---

## 2. BackBufferCopy <a name="back-buffer-copy"></a>

### Purpose

BackBufferCopy explicitly copies a region or the whole viewport into the back buffer so that screen-reading shaders have the correct source. It is not obsolete in Godot 4 — the need to control *when* the source image is copied still exists.

### When to Use

- Multiple overlapping screen-readers that must visually stack — place a BackBufferCopy in draw order between them so the later one samples a refreshed back buffer
- Local distortions that need a tightly bounded copy region instead of a full viewport copy
- Regional BackBufferCopy is more efficient than always running a full-screen ColorRect

### Configuration

- `copy_mode`: `RECT` (copy a rectangular region) or `VIEWPORT` (copy the whole viewport)
- For `RECT` mode, set the rect to cover the effect geometry plus padding for maximum sample radius
- Place the BackBufferCopy node **before** the screen-reading node in draw order

---

## 3. CanvasGroup Compositing <a name="canvas-group"></a>

### What It Does

CanvasGroup merges multiple child CanvasItem nodes into a single 2D draw operation. Overlapping translucent children are treated as one composed image — the overlap area does not become unintentionally more opaque when you fade or tint the whole effect.

### When to Use

Ideal for actor-centered effect bundles: layered slash trails, aura sprites, wisps, additive particles, rings, and impact decals that should first merge together and only then receive a group-wide dissolve, blur, tint, or edge treatment.

### Custom Group Shader

CanvasGroup uses a built-in back-buffer-reading shader internally. Assigning your own material replaces it. Preserve the alpha recovery step:

```glsl
shader_type canvas_item;
render_mode unshaded;

uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_nearest_mipmap;
uniform float lod = 1.0;
uniform vec4 tint : source_color = vec4(1.0, 0.95, 1.1, 1.0);

void fragment() {
    vec4 grp = textureLod(screen_tex, SCREEN_UV, lod);

    // Alpha recovery — required to preserve correct compositing
    if (grp.a > 0.0001) {
        grp.rgb /= grp.a;
    }

    grp.rgb *= tint.rgb;
    grp.a *= tint.a;

    COLOR *= grp;
}
```

### Sizing Properties

| Property | Purpose | Guidance |
|----------|---------|----------|
| `fit_margin` | Expands drawable rect around fitted child bounds | Raise when shader needs room beyond children's natural bounds (blur, dilation, glow) |
| `clear_margin` | Expands back-buffer area used by the group | Raise cautiously if you see edge artifacts |
| `use_mipmaps` | Generate mipmaps for group back buffer before drawing | Enable for group blur or softened refraction; leave off for tint/fade-only passes |

### Caveat: clip_children

CanvasGroup and `CanvasItem.clip_children` both rely on the back buffer. Clipped children inside a CanvasGroup do not behave correctly unless `clip_children` is disabled. If you need both clipping and group compositing, use a SubViewport instead.

---

## 4. SubViewport Isolation <a name="sub-viewport"></a>

### What It Does

SubViewport renders nodes below it in the scene hierarchy into a separate texture. To see the result, place it in a SubViewportContainer or assign its ViewportTexture to a Sprite2D, TextureRect, or material input.

### 2D VFX Use Cases

- Render VFX at a different internal resolution (low-res glow fields, soft afterimages, pixelated smears)
- Effect-only masks or silhouette passes via `canvas_cull_mask`
- Persistent trail or smear buffers by not clearing every frame
- Isolation boundary so VFX rendering does not affect the main scene

### Configuration

```gdscript
@onready var fx_viewport: SubViewport = $FxViewport

func _ready() -> void:
    fx_viewport.disable_3d = true
    fx_viewport.transparent_bg = true

    fx_viewport.size = Vector2i(640, 360)
    fx_viewport.size_2d_override = Vector2i(320, 180)
    fx_viewport.size_2d_override_stretch = true

    fx_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
    fx_viewport.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
```

### Canvas Visibility Layers

`Viewport.canvas_cull_mask` decides which CanvasItem rendering layers a viewport draws. Each CanvasItem has its own `visibility_layer`. This enables rendering just one VFX layer into a SubViewport and ignoring the rest of the scene.

Caveat: CanvasItem visibility layers do not automatically inherit through parents. Parents and children must share the required layers, or the child must be `top_level`. Since `top_level` breaks transform inheritance and changes draw order, use it as an escape hatch, not a default.

### Update and Clear Modes

| Mode | Use Case |
|------|----------|
| `UPDATE_ALWAYS` | Standard real-time VFX buffer |
| `UPDATE_ONCE` | Freeze a generated effect texture after one render |
| `UPDATE_WHEN_VISIBLE` | Dormant until visible — save cost on off-screen effects |
| `CLEAR_MODE_ALWAYS` | Standard — fresh buffer each frame |
| `CLEAR_MODE_NEVER` | Persistent trail or smear buffers that accumulate over time |
| `CLEAR_MODE_ONCE` | Clear once then accumulate |

### Transparent Background

`transparent_bg = true` is usually correct for 2D VFX buffers. The documented limitations (SSR, SSS, DOF disabled) are 3D-oriented features — for a pure 2D buffer with `disable_3d = true`, the trade-off is acceptable.

---

## 5. Full-Screen Post-Processing <a name="full-screen-post"></a>

### Standard Path

Put a ColorRect inside a CanvasLayer, set the rectangle to full rect, and attach a `canvas_item` shader that samples `hint_screen_texture`. This is the correct route for CRT simulation, camera-wide chromatic aberration, speed lines, bloom-like stylization, vignette, and palette remapping.

### Hierarchy Control

CanvasLayer ordering is explicit and stronger than per-item Z. A post-process CanvasLayer above the game world but below the HUD will process the world and leave the HUD crisp. This is the cleanest way to stop a camera effect from accidentally breaking UI readability.

### Render Modes for Post-Processing

| Render Mode | Use Case |
|-------------|----------|
| `unshaded` | Disables 2D lighting — correct for most screen-space post-processes |
| `blend_disabled` | Writes result directly — frame-replacing effects (CRT, color grading, distortion) |
| `blend_mix` | Standard alpha blend — overlay effects that partially cover the frame |
| `blend_mul` | Multiply blend — darkening/tinting overlays |
| `blend_premul_alpha` | Premultiplied alpha — mixed additive/blend overlays |

### Practical Vignette Shader

```glsl
shader_type canvas_item;
render_mode unshaded, blend_disabled;

uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_nearest;
uniform float vignette_strength = 0.35;

void fragment() {
    vec3 colour = texture(screen_tex, SCREEN_UV).rgb;

    vec2 p = SCREEN_UV * 2.0 - 1.0;
    float edge = smoothstep(0.7, 1.0, dot(p, p));

    colour *= mix(1.0, 1.0 - vignette_strength, edge);

    COLOR = vec4(colour, 1.0);
}
```

### Production Pattern

Separate frame-replacing and overlay passes:
- **Frame-replacing** (CRT curvature, scanline modulation, color grading, camera-wide distortion): `blend_disabled` full-screen shader
- **Overlay** (speed lines, damage flashes, soft vignette rings): later overlay passes with appropriate blend mode

---

## 6. Multi-Pass Stacking <a name="multi-pass"></a>

Godot supports multi-pass full-screen effects by stacking multiple CanvasLayer + ColorRect pairs. Each pass takes the previous pass as input. Order depends on the positions of the stacked CanvasLayer nodes in the scene tree.

### Recommended Stack Order

```text
Root
├─ World                       # default canvas / CanvasLayer 0
│  ├─ Actors
│  ├─ LocalShockwave + BackBufferCopy
│  ├─ SlashFxGroup (CanvasGroup)
│  └─ LowResAuraComposite      # Sprite2D using SubViewport's ViewportTexture
├─ CameraPost                  # CanvasLayer 1
│  ├─ CRTPass (ColorRect)      # blend_disabled — frame-replacing
│  └─ SpeedLinesPass (ColorRect) # blend_mix — overlay
└─ HUD                         # CanvasLayer 2
   └─ UI
```

### Limitation

Godot's custom post-processing path only sees the already-rendered frame and buffers Godot exposes as samplers. There is no arbitrary access to extra render targets (no unexposed normal or depth buffers in 2D). When you need extra masks or intermediate buffers, use SubViewport.

---

## 7. HDR 2D <a name="hdr-2d"></a>

`Viewport.use_hdr_2d` switches 2D rendering to an HDR `RGBA16` framebuffer in linear space. This:
- Enables effects that need high dynamic range such as 2D glow
- Substantially improves highly detailed gradients
- Allows emission values > 1.0 to trigger bloom-like effects

Enable on the root viewport or a relevant isolated effect viewport when the polish stack depends on smooth glows, screen-fades, or subtle color ramps.

---

## 8. Production Scene Hierarchy <a name="production-hierarchy"></a>

Think in terms of nested compositing scopes rather than one shader per node:

1. **World content and local distortions** on the base canvas — use `hint_screen_texture` directly on effect geometry, introduce BackBufferCopy only when overlap or region control matters
2. **Actor-centered grouped effects** in CanvasGroups — apply fades, dissolves, blur, or unified tinting on the group, not on every child
3. **Isolated VFX buffers** in SubViewports — composite the ViewportTexture back into the world at the exact layer where it belongs
4. **Camera polish** on a higher CanvasLayer — keep full-screen ColorRect passes sparse and intentional
5. **HUD/UI** on the highest CanvasLayer you want to protect

For UI protection, solve the problem with layer ordering before attempting shader hacks. Put the HUD on a higher CanvasLayer if it must remain readable. A CanvasLayer belongs to one specific viewport and is not shared automatically across viewports.

The short version: use `hint_screen_texture` to sample, `BackBufferCopy` to control source freshness, `CanvasGroup` to merge local translucent stacks, `SubViewport` to create isolated buffers, and `CanvasLayer` + `ColorRect` to stylize whole layers.
