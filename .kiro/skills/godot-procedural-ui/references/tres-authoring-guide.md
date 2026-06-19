# .tres Authoring Guide

How to create and edit Procedural Shader Utility `.tres` resource files directly in text format. Use this when creating styles programmatically or when the Godot editor is not available.

## Godot .tres Format Basics

A `.tres` file is a text-based Godot resource. Structure:

```
[gd_resource type="Resource" script_class="ClassName" load_steps=N format=3]

[ext_resource type="Script" path="res://path/to/script.gd" id="1"]

[sub_resource type="Resource" id="unique_id"]
script = ExtResource("1")
property = value

[resource]
script = ExtResource("N")
property = value
```

- `[gd_resource]` — file header. `load_steps` = total ext_resource + sub_resource + 1.
- `[ext_resource]` — external file references (scripts, textures).
- `[sub_resource]` — inline resources (primitives, shapes, paints).
- `[resource]` — the root resource of this file.

## Script Paths

All procedural UI resources reference these scripts:

| Resource | Script path |
|----------|-------------|
| ProceduralPrimitive2D | `res://addons/procedural_shader_utility/core/procedural_primitive_2d.gd` |
| ProceduralShape2D | `res://addons/procedural_shader_utility/core/procedural_shape_2d.gd` |
| ProceduralPaint2D | `res://addons/procedural_shader_utility/paint_2d/procedural_paint_2d.gd` |
| ProceduralStyle2D | `res://addons/procedural_shader_utility/paint_2d/procedural_style_2d.gd` |
| ProceduralStyleVariant2D | `res://addons/procedural_shader_utility/paint_2d/procedural_style_variant_2d.gd` |

## Value Serialization

| GDScript type | .tres format |
|---------------|-------------|
| `int` | `0`, `1`, `2` |
| `float` | `1.0`, `0.5`, `0.08` |
| `bool` | `true`, `false` |
| `Color` | `Color(0.2, 0.5, 0.9, 1.0)` — RGBA floats 0–1 |
| `Vector2` | `Vector2(0.5, 0.5)` |
| `Vector4` | `Vector4(0.08, 0.08, 0.08, 0.08)` |
| `Array` (sub-resources) | `[SubResource("id1"), SubResource("id2")]` |
| `Resource` reference | `SubResource("id")` or `ExtResource("id")` |
| `null` | omit the property entirely (uses default) |

## Enum Values (integers in .tres)

### ProceduralPrimitive2D.Type
| Name | Value |
|------|-------|
| ROUNDED_RECT | `0` |
| RECT | `1` |
| CIRCLE | `2` |
| CAPSULE | `3` |

### ProceduralPrimitive2D.Operation
| Name | Value |
|------|-------|
| UNION | `0` |
| SUBTRACT | `1` |
| INTERSECT | `2` |

### ProceduralPaint2D.PaintMode
| Name | Value |
|------|-------|
| NONE | `0` |
| SOLID | `1` |
| GRADIENT | `2` |
| GRADIENT_2COLOR | `3` (internal — don't set manually, compiler auto-detects) |

### ProceduralPaint2D.GradientType
| Name | Value |
|------|-------|
| LINEAR | `0` |
| RADIAL | `1` |
| ANGULAR | `2` |
| DIAMOND | `3` |
| SQUARE | `4` |

### ProceduralPaint2D.PatternType
| Name | Value |
|------|-------|
| NONE | `0` |
| NOISE | `1` |
| DOTS | `2` |
| STRIPES | `3` |

## Complete .tres Templates

### ProceduralStyle2D (standalone file)

```tres
[gd_resource type="Resource" script_class="ProceduralStyle2D" load_steps=6 format=3]

[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_primitive_2d.gd" id="1"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_shape_2d.gd" id="2"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_paint_2d.gd" id="3"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_style_2d.gd" id="4"]

[sub_resource type="Resource" id="prim_0"]
script = ExtResource("1")
type = 0
operation = 0
center = Vector2(0.5, 0.5)
size = Vector2(1.0, 1.0)
rotation_deg = 0.0
corner_radii = Vector4(0.08, 0.08, 0.08, 0.08)
smoothness_px = 0.0
offset_px = 0.0
onion_thickness_px = 0.0
ripple_count = 0
ripple_width_px = 0.0

[sub_resource type="Resource" id="shape_0"]
script = ExtResource("2")
primitives = [SubResource("prim_0")]
sdf_offset_px = 0.0
onion_thickness_px = 0.0
ripple_count = 0
ripple_width_px = 0.0
morph_enabled = false
morph_index = 1
morph_t = 0.0

[sub_resource type="Resource" id="paint_0"]
script = ExtResource("3")
fill_mode = 1
fill_color = Color(0.2, 0.5, 0.9, 1.0)
stroke_mode = 0
stroke_color = Color(0.0, 0.0, 0.0, 1.0)
stroke_width_px = 0.0
feather_px = 1.0
shadow_enabled = false
shadow_color = Color(0.0, 0.0, 0.0, 0.6)
shadow_offset_px = Vector2(0.0, 4.0)
shadow_blur_px = 8.0
outer_glow_enabled = false
outer_glow_color = Color(1.0, 1.0, 1.0, 0.8)
outer_glow_radius_px = 6.0
inner_glow_enabled = false
inner_glow_color = Color(1.0, 1.0, 1.0, 0.5)
inner_glow_radius_px = 6.0
pattern_type = 0
pattern_scale = 10.0
pattern_opacity = 0.3
pattern_angle_deg = 0.0
hsv_hue_shift_deg = 0.0
hsv_saturation_scale = 1.0
hsv_value_scale = 1.0

[resource]
script = ExtResource("4")
shape = SubResource("shape_0")
paint = SubResource("paint_0")
```

### ProceduralStyleVariant2D (standalone file)

```tres
[gd_resource type="Resource" script_class="ProceduralStyleVariant2D" load_steps=2 format=3]

[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_style_variant_2d.gd" id="1"]

[resource]
script = ExtResource("1")
fill_color_override = true
fill_color = Color(0.78, 0.15, 0.19, 1.0)
```

Only include fields you want to override. Omitted fields use defaults (override = false).

### Gradient Resource (inline in paint)

```tres
[sub_resource type="Gradient" id="my_gradient"]
offsets = PackedFloat32Array(0.0, 1.0)
colors = PackedColorArray(0.1, 0.7, 0.4, 1.0, 0.0, 0.3, 0.8, 1.0)
```

The `colors` array is flat RGBA: `r1, g1, b1, a1, r2, g2, b2, a2, ...`

Reference it from paint:
```tres
fill_gradient = SubResource("my_gradient")
fill_gradient_type = 0
fill_gradient_rotation_deg = 45.0
```

For multi-stop gradients:
```tres
[sub_resource type="Gradient" id="multi_grad"]
offsets = PackedFloat32Array(0.0, 0.5, 1.0)
colors = PackedColorArray(1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0)
```

## Minimal Examples

### Solid Rounded Button Style

```tres
[gd_resource type="Resource" script_class="ProceduralStyle2D" load_steps=6 format=3]

[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_primitive_2d.gd" id="1"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_shape_2d.gd" id="2"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_paint_2d.gd" id="3"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_style_2d.gd" id="4"]

[sub_resource type="Resource" id="prim_0"]
script = ExtResource("1")
type = 0
center = Vector2(0.5, 0.5)
size = Vector2(1.0, 1.0)
corner_radii = Vector4(0.08, 0.08, 0.08, 0.08)

[sub_resource type="Resource" id="shape_0"]
script = ExtResource("2")
primitives = [SubResource("prim_0")]

[sub_resource type="Resource" id="paint_0"]
script = ExtResource("3")
fill_mode = 1
fill_color = Color(0.2, 0.5, 0.9, 1.0)
stroke_mode = 1
stroke_color = Color(0.1, 0.3, 0.7, 1.0)
stroke_width_px = 2.0
feather_px = 1.0

[resource]
script = ExtResource("4")
shape = SubResource("shape_0")
paint = SubResource("paint_0")
```

### Card with Shadow

```tres
[gd_resource type="Resource" script_class="ProceduralStyle2D" load_steps=6 format=3]

[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_primitive_2d.gd" id="1"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_shape_2d.gd" id="2"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_paint_2d.gd" id="3"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_style_2d.gd" id="4"]

[sub_resource type="Resource" id="prim_0"]
script = ExtResource("1")
type = 0
center = Vector2(0.5, 0.5)
size = Vector2(0.9, 0.9)
corner_radii = Vector4(0.06, 0.06, 0.06, 0.06)

[sub_resource type="Resource" id="shape_0"]
script = ExtResource("2")
primitives = [SubResource("prim_0")]

[sub_resource type="Resource" id="paint_0"]
script = ExtResource("3")
fill_mode = 1
fill_color = Color(1.0, 1.0, 1.0, 1.0)
feather_px = 1.0
shadow_enabled = true
shadow_color = Color(0.0, 0.0, 0.0, 0.3)
shadow_offset_px = Vector2(0.0, 3.0)
shadow_blur_px = 6.0

[resource]
script = ExtResource("4")
shape = SubResource("shape_0")
paint = SubResource("paint_0")
```

### Boolean Cutout with Outer Glow

```tres
[gd_resource type="Resource" script_class="ProceduralStyle2D" load_steps=6 format=3]

[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_primitive_2d.gd" id="1"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/core/procedural_shape_2d.gd" id="2"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_paint_2d.gd" id="3"]
[ext_resource type="Script" path="res://addons/procedural_shader_utility/paint_2d/procedural_style_2d.gd" id="4"]

[sub_resource type="Resource" id="prim_0"]
script = ExtResource("1")
type = 0
center = Vector2(0.5, 0.5)
size = Vector2(1.0, 1.0)
corner_radii = Vector4(0.1, 0.1, 0.1, 0.1)

[sub_resource type="Resource" id="prim_1"]
script = ExtResource("1")
type = 2
operation = 1
center = Vector2(0.85, 0.15)
size = Vector2(0.2, 0.2)

[sub_resource type="Resource" id="shape_0"]
script = ExtResource("2")
primitives = [SubResource("prim_0"), SubResource("prim_1")]

[sub_resource type="Resource" id="paint_0"]
script = ExtResource("3")
fill_mode = 1
fill_color = Color(0.15, 0.15, 0.2, 1.0)
feather_px = 1.0
outer_glow_enabled = true
outer_glow_color = Color(0.3, 0.5, 1.0, 0.6)
outer_glow_radius_px = 8.0

[resource]
script = ExtResource("4")
shape = SubResource("shape_0")
paint = SubResource("paint_0")
```

## Rules and Gotchas

1. **Only include non-default properties.** Godot omits default values on save. You can include them for clarity, but omitting them produces cleaner files.

2. **`load_steps` must be accurate.** Count: all `[ext_resource]` + all `[sub_resource]` + 1 (for `[resource]`).

3. **IDs must be unique within the file.** Use descriptive IDs like `"prim_0"`, `"shape_0"`, `"paint_0"` for readability.

4. **Enum values are integers.** `type = 0` not `type = ROUNDED_RECT`. See the enum tables above.

5. **`script_class` in the header is optional** but helps Godot identify the resource type without loading. Include it for standalone `.tres` files.

6. **Gradient `colors` is flat RGBA.** A 2-color gradient has 8 floats: `PackedColorArray(r1, g1, b1, a1, r2, g2, b2, a2)`.

7. **Don't set `fill_mode = 3` (GRADIENT_2COLOR) manually.** The compiler auto-detects 2-stop gradients. Always author as `fill_mode = 2` (GRADIENT) with a 2-stop gradient resource.

8. **Shadow/glow need space.** If shadow or outer glow extends beyond the node bounds, it gets clipped. Use `size` slightly smaller than `(1.0, 1.0)` or ensure the Control node has padding.

9. **Per-primitive modifiers default to 0.** Omit them for solid primitives — only include when you need offset/onion/ripple on a specific primitive.

10. **Post-SDF operations default to 0/false.** Omit them for standard shapes.

11. **After writing a .tres file, call `refresh_filesystem`** so the Godot editor picks up the change.

## Inline vs Standalone Resources

**Standalone `.tres` files** — use when the resource is shared across multiple scenes/nodes:
```
res://styles/button_default.tres
res://styles/variant_hover.tres
```

**Inline `[sub_resource]` in `.tscn`** — use for one-off styles specific to a single scene. The resource lives inside the scene file and isn't reusable elsewhere.

Reference a standalone `.tres` from a scene:
```tres
[ext_resource type="Resource" path="res://styles/button_default.tres" id="style_1"]

[node name="Button" type="Control" parent="."]
script = ExtResource("panel_script")
style = ExtResource("style_1")
```
