# 2D VFX Optimization Reference

Overdraw, batching, blend modes, pixel-art constraints, particle budgets, trail performance, and the 60fps checklist for 2D VFX in Godot 4.

## Table of Contents

1. [Why Overdraw Dominates 2D VFX](#overdraw)
2. [What Breaks 2D Batches](#batching)
3. [Blend Modes and Performance](#blend-modes)
4. [Particle Budget Guidelines](#particle-budgets)
5. [Trail Performance](#trail-performance)
6. [Pixel-Art Constraints](#pixel-art)
7. [Light2D Optimization](#lighting)
8. [60fps Checklist](#checklist)

---

## 1. Why Overdraw Dominates 2D VFX <a name="overdraw"></a>

### No Depth Buffer in 2D

Godot's 2D renderer uses a separate buffer and does not use a depth buffer. Translucent VFX obey painter's order — stacked particles, soft glows, smoke layers, and additive flashes keep redrawing the same screen pixels. There is no depth test to reject hidden fragments.

### Fill Rate Is the Real Enemy

A particle system made of large soft quads can be slower than a "more complex" effect made of tighter shapes. When drawing large transparent images, the whole quad is drawn and the transparent area still costs pixels. Godot recommends converting such sprites to 2D meshes so only the opaque parts are drawn.

### Measuring Overdraw

Overdraw factor = number of transparent fragment shader executions per screen pixel at peak density. If your VFX performance rises sharply when you test in a much smaller window, you are fill-rate limited.

### Reducing Overdraw

Ordered by impact:
1. **Reduce particle size** — smaller quads cover fewer pixels
2. **Reduce particle count** — fewer overlapping layers
3. **Shorten lifetimes** — less accumulation over time
4. **Tighten visibility_rect** — system stops rendering when rect is outside viewport
5. **Use additive sprites for transient flashes** instead of real Light2D nodes
6. **SubViewport downscaling** — render dense effects at half resolution, composite back

### amount vs amount_ratio

Reduce `amount` to lower GPU cost. `amount_ratio` does **not** improve performance — resources are still allocated and processed for the full `amount`. Very high `preprocess` values are documented as potentially very expensive and can crash the GPU.

---

## 2. What Breaks 2D Batches <a name="batching"></a>

All rendering methods in current Godot 4 feature 2D batching, but only when VFX remain consistent in material, texture state, primitive type, and draw order.

### Batch Breakers

| Batch Breaker | Why It Matters |
|---|---|
| **Material or shader change** | The RD canvas renderer starts a new batch when the material RID changes. Different materials = different batches. |
| **Blend mode change** | `blend_mix`, `blend_add`, `blend_sub`, `blend_mul`, `blend_premul_alpha`, and `blend_disabled` are distinct render modes — separate state groups. |
| **Texture, filter, or repeat change** | Godot's TextureState includes the texture, `texture_filter`, and `texture_repeat`. Changing any of them splits batches. |
| **Primitive type or point-count change** | `draw_primitive()` batching only works when successive primitives keep compatible command type and same point count. |
| **Polygon-based drawing** | The current RD renderer cannot batch polygons. `draw_polygon()` trail segments always create a new batch. |
| **Lighting and clip boundaries** | Different light usage or clip owners force a new batch even when texture and material match. |
| **Z-index fragmentation** | Even if state matches, painter-order constraints stop similar effects from remaining contiguous when spread across many Z bands. |

### Batch-Friendly Practices

**Do:**
- Share one CanvasItemMaterial or shader per VFX family
- Keep texture, filter, and repeat settings consistent across sibling VFX
- Organize effects into a few coarse Z bands instead of many tiny ones
- Use additive sprites for transient flashes — much faster than real 2D lights
- For custom trails, prefer repeated same-format primitives over polygon-per-segment

**Don't:**
- Give every emitter its own unique material for one small visual tweak
- Mix nearest and linear filter overrides within the same VFX layer
- Alternate blend modes constantly within one effect stack
- Build every custom trail segment as a polygon
- Assume `amount_ratio` is a performance knob

---

## 3. Blend Modes and Performance <a name="blend-modes"></a>

### CanvasItemMaterial Blend Modes

| Mode | Behavior | VFX Use |
|------|----------|---------|
| `Mix` | Standard alpha blending | Default for most effects |
| `Add` | Additive blending | Glows, energy, fire, fake lights |
| `Sub` | Subtractive blending | Shadows, darkening effects |
| `Mul` | Multiply blending | Darkening overlays, tinting |
| `Premul Alpha` | Pre-multiplied alpha | Mixed additive/blend in one material (fire core + smoke body) |

### Blend Mode as Batch Boundary

Each blend mode is a distinct render state. Group additive effects together, alpha-mix effects together. Interleaving blend modes within one effect stack fragments batches.

### Premultiplied Alpha

A single material exhibits both additive blending (bright pixels) and standard mix blending (dark pixels) simultaneously. Useful for fire, explosions, and magical energy where the core is emissive and the periphery is absorptive. Halves particle count for mixed-energy effects.

---

## 4. Particle Budget Guidelines <a name="particle-budgets"></a>

### Frame Budget

At 60fps, total frame time = 16.67ms. In 2D, VFX cost is dominated by fill rate and overdraw, not simulation.

### Budget by Effect Type

| Effect Type | Suggested Max Particles | Rationale |
|---|---|---|
| Dense smoke/fog | 30–100 | High overdraw, large soft quads |
| Spark shower | 100–500 | Small particles, low per-particle overdraw |
| Rain/snow | 200–1000 | Small, simple shader, low overdraw |
| Ambient dust | 50–200 | Persistent, must be cheap |
| Trail particles | 30–100 per trail | Skinned mesh adds cost per section |
| Burst (explosion) | 50–200 | Short-lived, acceptable spike |

These are rough guidelines — actual budgets depend on particle size, shader complexity, lighting, and target hardware.

### The Real Levers

When a particle system is too expensive:
1. Reduce `amount` (the real particle count)
2. Reduce particle size (less fill-rate cost per particle)
3. Shorten lifetime (less accumulation)
4. Tighten `visibility_rect` (cull when off-screen)
5. Reduce PointLight2D coverage on VFX layers
6. Replace real lights with additive fake glows
7. Switch to CPUParticles2D if GPU is the bottleneck

---

## 5. Trail Performance <a name="trail-performance"></a>

### Line2D

- `round_precision` raises render and update cost
- `antialiased = true` generates extra geometry, not accelerated by batching
- Best for ≤5 concurrent dynamic trails

### GPUParticles2D Trails

- `trail_sections` and `trail_section_subdivisions` trade performance for smoother curves
- Keep sections low for pixel art
- Best for many concurrent trails (10+)

### Custom _draw()

- Polygon-based trails are unbatchable — avoid `draw_polygon()` per segment
- Repeated same-format `draw_primitive()` calls can batch when texture state is constant
- Static geometry is cheap; fully dynamic redraws every frame are CPU work
- Best for pixel-locked thin lines where exact control matters

---

## 6. Pixel-Art Constraints <a name="pixel-art"></a>

### Filtering

In Godot 4, 2D texture filtering is controlled on CanvasItem nodes with a project setting as default. Pixel-art VFX baseline:
- `TEXTURE_FILTER_NEAREST` on all VFX nodes
- Lossless texture imports — VRAM compression introduces visible artifacts in low-res art
- No accidental filter overrides on one emitter while the rest uses another setting

### Viewport and Scaling

- `viewport` stretch mode with `integer` stretch scale mode — fractional scaling produces uneven pixel widths
- 640×360 is a practical base viewport for pixel art (scales cleanly to common resolutions)
- On Windows, use Exclusive Fullscreen instead of ordinary Fullscreen — especially important with integer scaling
- `canvas_items` mode only if comfortable with sub-pixel movement and rotation

### Transform Snapping

- `snap_2d_transforms_to_pixel` and/or `snap_2d_vertices_to_pixel` enabled
- Sprite2D centered textures may appear deformed because position falls between pixels — disable centering or enable snapping
- Camera2D stored position may differ from actual on-screen center when smoothing is active — this causes "particles shimmer when camera moves"

### Particle Motion

For strict pixel-art look, the answer is often the opposite of the default smoothing stack:
- Use integer camera/emitter positions
- Nearest filtering everywhere
- `fixed_fps` with `interpolate` disabled for stepped motion
- Usually disable `Fract Delta`
- If project-wide physics interpolation is enabled, disable it on GPUParticles2D nodes or convert to CPUParticles2D when smooth interpolation is truly required

### Physics Interpolation

GPUParticles2D does not yet support 2D physics interpolation. CPUParticles2D exposes `physics_interpolation_mode` in current stable. If you need interpolated particles in a pixel-art project, CPUParticles2D is the only option.

---

## 7. Light2D Optimization <a name="lighting"></a>

### Cost Model

PointLight2D becomes more expensive as it covers more pixels. Dense translucent particle layers under large 2D lights are often fill-rate and overdraw limited, not simulation limited.

### Optimization Strategies

1. **Light mask partitioning** — keep expensive normal-mapped hero VFX on one mask, push cheap ambience onto masks with little or no lighting
2. **Reduce light coverage** — smaller lights cost less
3. **Additive sprites instead of real lights** — for transient flashes, muzzle lights, explosions. Much faster but no normal/specular interaction
4. **Raise light height** — default height makes normal mapping barely visible; raise it for readable normal maps, but only on effects that need it

---

## 8. 60fps Checklist <a name="checklist"></a>

Before shipping any 2D VFX:

- [ ] **Overdraw**: estimate peak transparent layer count at the densest screen region
- [ ] **Fill rate**: if performance drops in larger windows, you are fill-rate limited — reduce particle size and overlap first
- [ ] **Particle count**: reduce `amount`, not `amount_ratio`; shorten lifetimes; keep `visibility_rect` tight; avoid very large `preprocess` values
- [ ] **Particle backend**: GPUParticles2D by default; test CPUParticles2D if weakest device is GPU-bound
- [ ] **Batching**: materials shared, blend modes grouped, texture state consistent, filter/repeat settings uniform across sibling VFX
- [ ] **Z-index**: few coarse Z bands for VFX families, not many tiny ones scattered across the draw order
- [ ] **Trails**: Line2D for few premium trails, particle trails for many concurrent streaks, custom `_draw()` only for pixel-locked control; no polygon-per-segment trails
- [ ] **Pixel art**: nearest filtering, lossless imports, viewport stretch mode, integer scale mode, transform/vertex snapping, no unsnapped camera smoothing
- [ ] **Lighting**: light mask partitioning, additive sprites for transient effects, reduced PointLight2D coverage
- [ ] **Screen-space effects**: sparse and intentional full-screen passes, BackBufferCopy only when overlap matters
- [ ] **Tween VFX**: one writer per property, baseline values stored, previous tweens killed before new ones
- [ ] **Approximation over correctness**: additive sprites for short-lived light-like effects, fake shadows instead of real particle collision
