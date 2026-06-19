# Shape and Style Catalog

Quick reference for the Procedural Shader Utility addon's resource system.

## Primitives

All coordinates are in normalized UV space (0–1). Center `(0.5, 0.5)` is the middle of the node. Size `(1.0, 1.0)` fills the entire node.

| Type | Class constant | Best for |
|------|---------------|----------|
| Rounded Rect | `ROUNDED_RECT` | Buttons, cards, panels, inputs |
| Rect | `RECT` | Dividers, flat-edged containers (ignores corner_radii entirely) |
| Circle | `CIRCLE` | Badges, avatars, indicators (forces equal dimensions via min(w,h)) |
| Capsule | `CAPSULE` | Pills, tags, chips, toggle tracks |

### Corner Radii (Rounded Rect only)

`corner_radii` is a `Vector4(top_left, top_right, bottom_right, bottom_left)`.

Common patterns:
```gdscript
Vector4(0.08, 0.08, 0.08, 0.08)  # uniform subtle rounding
Vector4(0.15, 0.15, 0.15, 0.15)  # pronounced rounding
Vector4(0.1, 0.1, 0.0, 0.0)      # tab shape: rounded top, flat bottom
Vector4(0.1, 0.1, 0.1, 0.0)      # chat bubble: one sharp corner
Vector4(0.5, 0.5, 0.5, 0.5)      # fully rounded (becomes capsule-like)
```

## Boolean Operations

Each primitive after the first carries an `operation`:

| Operation | What it does | Use case |
|-----------|-------------|----------|
| `UNION` | Merge shapes together | Compound shapes |
| `SUBTRACT` | Cut a hole | Badge slots, notches, cutouts |
| `INTERSECT` | Keep only overlap | Masked regions |

Order matters: first primitive is the base, subsequent ones modify it. Max 4 primitives per shape.

### Smooth Boolean Operations

Each primitive has a `smoothness_px` property (default 0.0 = hard edge). When set to a positive value, the boolean operation blends with a rounded transition of that radius in pixels.

| smoothness_px | Effect |
|---------------|--------|
| `0.0` | Hard boolean (default, identical to standard ops) |
| `5–15` | Subtle fillet/rounding at the junction |
| `20–50` | Pronounced organic blend |
| `50+` | Heavily melted/blobby shapes |

The value is converted to normalized local scale by the compiler (divided by `min(target_width, target_height)`), so the visual result scales with panel size.

Smooth booleans slightly expand the shape surface (smooth union pulls inward, smooth subtract rounds the carve edge). Hit testing accounts for this automatically.

## Paint

`ProceduralPaint2D` controls fill, stroke, and edge quality.

### Fill Modes

| Mode | When to use |
|------|------------|
| `NONE` | Stroke-only shapes (outlines, borders) |
| `SOLID` | Single-color fills — most common |
| `GRADIENT` | Styled surfaces, headers, hero elements |

### Stroke Modes

Same options as fill. Stroke is SDF-derived — it follows boolean operations and wraps around cutouts.

### Gradient Types

Requires a `Gradient` or `GradientTexture1D` resource assigned.

| Type | Effect |
|------|--------|
| `LINEAR` | Straight line, rotatable via `fill_gradient_rotation_deg` |
| `RADIAL` | Circular from center |
| `ANGULAR` | Conical sweep |
| `DIAMOND` | Diamond-shaped falloff |
| `SQUARE` | Rectangular contour — follows the shape of a box, natural for UI frames |

Gradients work on both fill and stroke independently.

**2-color fast path:** When a gradient has exactly 2 stops at offsets 0.0 and 1.0, the compiler auto-promotes it to `mix()` instead of texture sampling. No action needed — just use a 2-stop gradient normally.

### Gradient Transforms

Each gradient (fill and stroke independently) has:
- `offset` — Shifts the gradient origin in normalized space.
- `rotation_deg` — Rotates the gradient.
- `scale` — Scales the gradient UV.

### Gradient Resources

- `Gradient` — The compiler creates a `GradientTexture1D` internally (cached; stale entries pruned when source gradient is freed).
- `GradientTexture1D` — Passed through unchanged. Use for explicit control over texture size or filtering.

### Feather

`feather_px` controls edge softness in pixels. Always set to at least `1.0` for anti-aliased edges. The addon uses `fwidth()` as fallback, but explicit feather gives control.

### Drop Shadow

On `ProceduralPaint2D`:
- `shadow_enabled` — toggle
- `shadow_color` — Color with alpha (default: black 60%)
- `shadow_offset_px` — Vector2 offset in pixels (default: 0, 4)
- `shadow_blur_px` — blur radius in pixels (default: 8)

Shadow re-evaluates the SDF at an offset position — it perfectly follows boolean cutouts and all shape details.

### Outer Glow

On `ProceduralPaint2D`:
- `outer_glow_enabled` — toggle
- `outer_glow_color` — Color with alpha
- `outer_glow_radius_px` — how far the glow extends outside the shape

Renders behind the shape, in front of shadow. Follows post-SDF operations (offset/onion/ripple).

### Inner Glow

On `ProceduralPaint2D`:
- `inner_glow_enabled` — toggle
- `inner_glow_color` — Color with alpha
- `inner_glow_radius_px` — how far inward the glow extends

Mixed into fill color, strongest at shape edges. Creates inset lighting effects.

### Pattern Fill

On `ProceduralPaint2D` — modulates fill color (works with solid, gradient, or 2-color fills):
- `pattern_type` — `NONE`, `NOISE`, `DOTS`, `STRIPES`
- `pattern_scale` — repetition scale (higher = more repetitions)
- `pattern_opacity` — 0 = invisible, 1 = full strength
- `pattern_angle_deg` — rotation (mainly for stripes direction)

Use cases: paper/fabric texture, disabled state overlay, decorative backgrounds.

### HSV Color Adjustment

On `ProceduralPaint2D` — applied to fill color after patterns:
- `hsv_hue_shift_deg` — hue rotation (-180 to 180, 0 = no shift)
- `hsv_saturation_scale` — multiplier (1.0 = unchanged, 0 = grayscale)
- `hsv_value_scale` — brightness multiplier (1.0 = unchanged)

Use cases: runtime theming without new gradient resources, animated rainbow effects.

## Per-Primitive Modifiers

On `ProceduralPrimitive2D` — applied after SDF evaluation, before boolean composition:

| Field | Effect | Use case |
|-------|--------|----------|
| `offset_px` | Grow (negative) / shrink (positive) this primitive | Animated press state on one shape |
| `onion_thickness_px` | Hollow this primitive into a ring | Ring unioned with solid rect |
| `ripple_count` + `ripple_width_px` | Concentric rings from this primitive | Radio button rings |

These are independent from the post-SDF operations on `ProceduralShape2D`. Per-primitive modifiers affect individual primitives before booleans; post-SDF operations affect the final composed shape.

## Post-SDF Operations

On `ProceduralShape2D` — applied after all boolean composition:

| Field | Effect | Use case |
|-------|--------|----------|
| `sdf_offset_px` | Grow (negative) / shrink (positive) entire shape | Hover grow, press shrink |
| `onion_thickness_px` | Hollow entire composed shape | Ring from any boolean result |
| `ripple_count` + `ripple_width_px` | Concentric rings on composed shape | Multi-ring focus indicator |

### SDF Morph

On `ProceduralShape2D`:
- `morph_enabled` — toggle
- `morph_index` — which primitive (1-3) to blend with primitive 0
- `morph_t` — blend factor (0.0 = primitive 0, 1.0 = morph target)

The morph target is excluded from the boolean loop — it only participates via the blend. Animate `morph_t` for shape transitions (toggle switch: circle ↔ rounded rect).

## Style System

### Sharing Hierarchy

```
ProceduralStyle2D (.tres)          — base definition, shared by many nodes
    └── ProceduralStyleVariant2D   — lightweight color/paint overrides
        └── Instance Overrides     — per-node one-off tweaks (on panel/binder)
            └── Compiler Output    — final values
```

Later in the chain always wins. Instance overrides take highest precedence — they override both the base style and the variant.

### When to Use What

| Need | Approach |
|------|----------|
| Same shape + paint everywhere | One shared `.tres` style |
| Same shape, different colors per state | One style + variants (hover, danger, disabled) |
| One specific node differs slightly | Instance overrides on that node |
| Different shape entirely | Separate style |

### Variant Limitations

Variants can override paint properties (colors, stroke width, feather, gradients). They cannot change shape structure (primitive types, positions, boolean operations, corner radii).

### Style Library Convention

```
res://styles/
  card_default.tres           # ProceduralStyle2D
  card_paint.tres             # ProceduralPaint2D (shared across styles)
  rounded_shape.tres          # ProceduralShape2D
  variant_danger.tres         # ProceduralStyleVariant2D
  variant_success.tres
  variant_disabled.tres
```

## Nodes

### ProceduralPanel2D

A `Control` node. Participates in Godot's layout system (anchors, containers, margins). Provides shape-aware `_has_point()` — clicks register inside the visible shape boundary (fill + stroke + feather) with 1px tolerance. Clicks on transparent areas outside the shape are ignored.

Use for: all new UI elements.

### ProceduralCanvasBinder2D

A plain `Node` that applies a procedural style to an existing node via `target_path`. Takes over the target's material. Does NOT provide hit testing — the target keeps its own `_has_point()` behavior. If you need shape-aware input, use ProceduralPanel2D instead.

Use for: adding procedural rendering to nodes you can't replace (Sprite2D with existing logic, TextureRect you want to mask).

### Texture Modulation

A backend-only feature (not part of ProceduralPaint2D). Set `source_texture` on ProceduralPanel2D or ProceduralCanvasBinder2D to multiply the fill color by a texture. The compiler sets `use_source_texture = true` automatically when a texture is provided. For Sprite2D targets via binder, assign the sprite's texture as `source_texture` to blend it through the procedural shape (e.g., rounded-corner icon masking).

## Runtime API

```gdscript
# Style assignment
$MyPanel.style = preload("res://styles/card_default.tres")

# Variant assignment
$MyPanel.style_variant = preload("res://styles/variant_danger.tres")

# Instance overrides (per-node)
$MyPanel.fill_color_override = true
$MyPanel.fill_color = Color.YELLOW

# Binder setup
var binder := ProceduralCanvasBinder2D.new()
binder.target_path = NodePath("../MySprite")
binder.style = my_style
binder.source_texture = preload("res://icon.svg")
add_child(binder)

# Dynamic fill — fast path (no recompile)
$HPBar.set_fill_ratio(current_hp / max_hp)
$HPBar.set_secondary_fill_ratio(previous_hp / max_hp)

# Segment helpers
$AmmoBar.set_segments_from_count(current_ammo, max_ammo)
$AmmoBar.set_segment_filled(2, false)

# Directional indicator
$DamageIndicator.flash(damage_angle_deg)
```

## Dynamic Fill System

The fill clip system turns any procedural shape into a progress bar, cooldown ring, or segmented meter — without writing custom shaders.

### Fill Clip Modes

On `ProceduralPaint2D`:

| Mode | Use case |
|------|----------|
| `NONE` | No clipping (default, static shape) |
| `LINEAR` | Horizontal/vertical progress bars |
| `ANGULAR` | Radial cooldowns, timer rings |

### Fill Direction (LINEAR mode)

| Direction | Effect |
|-----------|--------|
| `LEFT_TO_RIGHT` | Standard left-to-right fill (default) |
| `RIGHT_TO_LEFT` | Right-to-left fill |
| `BOTTOM_TO_TOP` | Vertical fill upward |
| `TOP_TO_BOTTOM` | Vertical fill downward |

### Fill Clip Properties

| Property | Range | Effect |
|----------|-------|--------|
| `fill_clip_ratio` | 0.0–1.0 | How much is filled. Animate with `set_fill_ratio()` |
| `fill_clip_angle_offset_deg` | -180–180 | Start angle for ANGULAR mode (-90 = top) |
| `fill_clip_spin_speed` | degrees/sec | Auto-rotation for angular fill (loading spinners) |

### Secondary Fill (Ghost Bar)

Renders behind primary fill — used for damage preview, delayed drain visualization.

| Property | Effect |
|----------|--------|
| `secondary_fill_enabled` | Toggle |
| `secondary_fill_color` | Color of the ghost bar |
| `secondary_fill_ratio` | 0.0–1.0, animate with `set_secondary_fill_ratio()` |

Uses the same clip mode and direction as primary fill.

### Threshold Color Shift

Auto-shifts fill color when ratio drops below a threshold — low HP warning.

| Property | Effect |
|----------|--------|
| `fill_color_low` | Color to shift to (default: RED) |
| `fill_color_threshold` | Ratio below which shift occurs (0 = disabled) |

### Segmentation

Divides fill into discrete segments (stamina bars, ammo counters).

| Property | Effect |
|----------|--------|
| `fill_segments` | Number of segments (0 = disabled, max 32) |
| `fill_segment_gap` | Gap between segments (0.0–0.5, fraction of segment width) |
| `fill_segment_mask` | Bitmask: bit i = segment i filled. -1 = all filled |
| `fill_segment_partial_index` | Which segment is partially filling (-1 = none) |
| `fill_segment_partial_ratio` | Partial segment fill (0.0–1.0) |
| `fill_segment_empty_color` | Color for unfilled segments |

### Multi-Layer Fill

Up to 3 total fill layers (primary + 2 extra), composited back-to-front.

| Property | Effect |
|----------|--------|
| `fill_layer_2_enabled/color/ratio` | Second layer (behind primary + secondary) |
| `fill_layer_3_enabled/color/ratio` | Third layer (furthest back) |

Use for shield + armor + health stacking.

### Overflow State

| Property | Effect |
|----------|--------|
| `fill_overflow_glow` | When true and fill_clip_ratio >= 1.0, activates outer glow |

### Animated Glow Pulse

| Property | Effect |
|----------|--------|
| `outer_glow_pulse_speed` | Hz, 0 = disabled |
| `outer_glow_pulse_amplitude_px` | Pixels of radius oscillation |

### Animated Gradient Rotation

| Property | Effect |
|----------|--------|
| `fill_gradient_rotation_speed` | Degrees/second, 0 = disabled |

### Fill-Driven Morph

On `ProceduralShape2D`:

| Property | Effect |
|----------|--------|
| `morph_bind_to_fill` | When true, morph_t = fill_clip_ratio (shape changes as bar fills) |

### Conditional Effects

| Property | Values | Effect |
|----------|--------|--------|
| `inner_glow_region` | BOTH, FILLED_ONLY, EMPTY_ONLY | Restrict inner glow to fill region |
| `pattern_region` | BOTH, FILLED_ONLY, EMPTY_ONLY | Restrict pattern to fill region |

## ProceduralIndicator2D

Lightweight directional indicator node with its own dedicated shader. Used for damage direction indicators, compass markers, and ping effects.

| Property | Effect |
|----------|--------|
| `indicator_color` | Arc color |
| `indicator_angle_deg` | Direction to point at |
| `indicator_arc_deg` | Width of the arc (1–180°) |
| `indicator_inner_radius` | Inner edge (0 = center, 0.7 = outer ring) |
| `indicator_opacity` | Opacity (for fade-out) |
| `indicator_feather` | Edge softness |

Methods:
- `flash(angle_deg)` — Set angle and opacity to 1.0 (trigger from GDScript, tween opacity down for fade)


## Common Recipes

### Rounded Button
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.08, 0.08, 0.08, 0.08)
Paint: solid fill, 2px solid stroke (darker), feather 1.0
```

### Pill Tag
```
Primitive: CAPSULE, center (0.5, 0.5), size (1.0, 1.0)
Paint: solid fill, feather 1.0
```

### Card with Badge Cutout
```
Primitive 1: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.1, 0.1, 0.1, 0.1)
Primitive 2: CIRCLE, operation SUBTRACT, center (0.85, 0.15), size (0.25, 0.25)
Paint: solid fill, 2px stroke, feather 1.0
```

### Tab Shape
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.1, 0.1, 0.0, 0.0)
Paint: solid fill, feather 1.0
```

### Gradient Header Panel
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.1, 0.1, 0.0, 0.0)
Paint: gradient fill (LINEAR, 90°), 1px solid stroke, feather 1.0
```

### Organic Merged Shapes (Smooth Union)
```
Primitive 1: CIRCLE, center (0.35, 0.5), size (0.5, 0.5)
Primitive 2: CIRCLE, operation UNION, center (0.65, 0.5), size (0.5, 0.5), smoothness_px 20.0
Paint: solid fill, feather 1.0
```
The two circles merge with a rounded fillet instead of a sharp crease.

### Card with Drop Shadow
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (0.9, 0.9), corner_radii (0.08, 0.08, 0.08, 0.08)
Paint: solid fill, feather 1.0, shadow_enabled true, shadow_offset_px (2, 4), shadow_blur_px 8
```
Note: size is 0.9 to leave room for the shadow to render within the node bounds.

### Focus Ring (Ripple)
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (0.85, 0.85), corner_radii (0.08, 0.08, 0.08, 0.08)
Shape: ripple_count 2, ripple_width_px 3
Paint: solid stroke (2px), no fill, feather 1.0, outer_glow_enabled true, outer_glow_radius_px 4
```

### Toggle Switch (Morph)
```
Primitive 0: CAPSULE, center (0.5, 0.5), size (1.0, 1.0)  — track shape
Primitive 1: CIRCLE, center (0.3, 0.5), size (0.4, 0.4)   — knob (off position)
Shape: morph_enabled false (animate morph_t 0→1 to transition knob position)
```
For the knob, use a second ProceduralPanel2D and animate its position. Morph is better for shape transitions (e.g., square→circle knob).

### Hollow Ring (Onion)
```
Primitive: CIRCLE, center (0.5, 0.5), size (0.8, 0.8)
Shape: onion_thickness_px 4
Paint: solid fill, feather 1.0
```

### Striped Warning Panel
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.06, 0.06, 0.06, 0.06)
Paint: solid fill (yellow), pattern_type STRIPES, pattern_scale 20, pattern_opacity 0.3, pattern_angle_deg 45, feather 1.0
```

### Health Bar with Damage Preview
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.15, 0.15, 0.15, 0.15)
Paint: solid fill (green), fill_clip_mode LINEAR, fill_direction LEFT_TO_RIGHT,
       secondary_fill_enabled true, secondary_fill_color (red, 0.5 alpha),
       fill_color_threshold 0.3, fill_color_low Color.RED, feather 1.0
Runtime: set_fill_ratio(current_hp / max_hp), set_secondary_fill_ratio(prev_hp / max_hp)
```

### Radial Cooldown
```
Primitive: CIRCLE, center (0.5, 0.5), size (0.9, 0.9)
Paint: solid fill (dark overlay), fill_clip_mode ANGULAR, fill_clip_angle_offset_deg -90,
       feather 1.0
Runtime: set_fill_ratio(1.0 - cooldown_remaining / cooldown_max)
```

### Loading Spinner
```
Primitive: CIRCLE, center (0.5, 0.5), size (0.8, 0.8)
Shape: onion_thickness_px 4
Paint: solid fill, fill_clip_mode ANGULAR, fill_clip_ratio 0.25,
       fill_clip_angle_offset_deg -90, fill_clip_spin_speed 360.0, feather 1.0
```

### Segmented Stamina Bar
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.1, 0.1, 0.1, 0.1)
Paint: solid fill (yellow), fill_clip_mode LINEAR, fill_segments 5,
       fill_segment_gap 0.05, feather 1.0
Runtime: set_fill_ratio(current_stamina / max_stamina)
```

### Ammo Counter (Per-Segment State)
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.08, 0.08, 0.08, 0.08)
Paint: solid fill (white), fill_clip_mode LINEAR, fill_clip_ratio 1.0,
       fill_segments 8, fill_segment_gap 0.08, fill_segment_mask -1,
       fill_segment_empty_color (grey, 0.3 alpha), feather 1.0
Runtime: set_segments_from_count(current_ammo, max_ammo)
```

### Overcharge Meter (Overflow Glow)
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.1, 0.1, 0.1, 0.1)
Paint: solid fill (cyan), fill_clip_mode LINEAR, fill_overflow_glow true,
       outer_glow_color (cyan), outer_glow_radius_px 10,
       outer_glow_pulse_speed 2.0, outer_glow_pulse_amplitude_px 4, feather 1.0
Runtime: set_fill_ratio(charge / max_charge)  — glow activates at 1.0
```

### Triple-Stacked Health Bar (Shield + Armor + HP)
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (1.0, 1.0), corner_radii (0.12, 0.12, 0.12, 0.12)
Paint: solid fill (red), fill_clip_mode LINEAR,
       fill_layer_2_enabled true, fill_layer_2_color (grey, armor),
       fill_layer_3_enabled true, fill_layer_3_color (cyan, shield), feather 1.0
Runtime: set_fill_ratio(hp_ratio), layer 2/3 ratios for armor/shield
```

### Card Rarity Glow
```
Primitive: ROUNDED_RECT, center (0.5, 0.5), size (0.95, 0.95), corner_radii (0.08, 0.08, 0.08, 0.08)
Paint: gradient fill (ANGULAR), fill_gradient_rotation_speed 90.0,
       outer_glow_enabled true, outer_glow_color (gold), outer_glow_radius_px 6,
       outer_glow_pulse_speed 1.5, outer_glow_pulse_amplitude_px 3, feather 1.0
```

### Damage Direction Indicator
```
Node: ProceduralIndicator2D
Properties: indicator_color (red, 0.8 alpha), indicator_arc_deg 45,
            indicator_inner_radius 0.7, indicator_feather 0.1
Runtime: flash(damage_angle_deg), then tween indicator_opacity to 0 over 0.5s
```

## Animation Quick Reference

Paint properties are shader uniforms — tweening them is cheap and does not trigger recompilation.

### Hover Color Transition
```gdscript
# On hover enter — tween fill color
var tw := create_tween()
tw.tween_property($Panel, "material:shader_parameter/u_fill_color", hover_color, 0.15) \
    .set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
```

### Focus Ring Pop
```gdscript
# Stroke width needs manual pixel-to-local conversion
var dim := minf($Panel.size.x, $Panel.size.y)
var tw := create_tween()
tw.tween_method(func(v): $Panel.material.set_shader_parameter("u_stroke_width_px", v / dim),
    1.0, 3.0, 0.12).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
```

### Key Rules
- Kill the previous tween before starting a new one on the same uniform.
- Use `self_modulate` (not `modulate`) to tint only the panel without affecting child labels/icons.
- Wrap overlapping panels in `CanvasGroup` when fading them as a unit.

## Performance Quick Reference

| Concern | Threshold | Mitigation |
|---------|-----------|------------|
| Draw calls | 100+ visible panels | Bake static shapes via SubViewport + UPDATE_ONCE |
| Overdraw | 3+ overlapping panels | Reduce layer count or bake background layers |
| Gradient banding | Large panels, subtle gradients | Enable `Viewport.use_hdr_2d` |
| Instantiation hitch | 50+ panels created at once | Stagger across frames or pre-instantiate |
| Batch breaks | Mixed gradient filter modes | Keep filter settings consistent per layer |
