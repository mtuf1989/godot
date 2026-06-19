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

Gradients work on both fill and stroke independently.

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
```

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
