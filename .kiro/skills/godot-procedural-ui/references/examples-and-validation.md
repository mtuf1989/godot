# Examples and Validation

## Example 1: Themed Button Set with Variants

**Request:** "Create a set of rounded buttons — primary, danger, and disabled states — that share the same shape"

**Style library:**
```
res://styles/
  button_shape.tres          # ProceduralShape2D — shared rounded rect
  button_paint.tres          # ProceduralPaint2D — base paint (primary color)
  button_style.tres          # ProceduralStyle2D — shape + paint
  variant_danger.tres        # ProceduralStyleVariant2D — red fill override
  variant_disabled.tres      # ProceduralStyleVariant2D — grey fill, thinner stroke
```

**Shape composition:**
```gdscript
# button_shape.tres
var prim := ProceduralPrimitive2D.new()
prim.type = ProceduralPrimitive2D.Type.ROUNDED_RECT
prim.center = Vector2(0.5, 0.5)
prim.size = Vector2(1.0, 1.0)
prim.corner_radii = Vector4(0.08, 0.08, 0.08, 0.08)
```

**Variant setup:**
```gdscript
# variant_danger.tres
var danger := ProceduralStyleVariant2D.new()
danger.fill_color_override = true
danger.fill_color = Color(0.78, 0.15, 0.19)

# variant_disabled.tres
var disabled := ProceduralStyleVariant2D.new()
disabled.fill_color_override = true
disabled.fill_color = Color(0.5, 0.5, 0.5, 0.5)
disabled.stroke_width_px_override = true
disabled.stroke_width_px = 1.0
```

**Scene wiring:**
```gdscript
$PrimaryButton.style = preload("res://styles/button_style.tres")
$DangerButton.style = preload("res://styles/button_style.tres")
$DangerButton.style_variant = preload("res://styles/variant_danger.tres")
$DisabledButton.style = preload("res://styles/button_style.tres")
$DisabledButton.style_variant = preload("res://styles/variant_disabled.tres")
```

**Why variants instead of separate styles:** One shape definition, three color states. Change the corner radius once, all buttons update. Variants are cheap — they only store overridden fields.

**Validation checklist:**
- [ ] All three buttons render with correct colors in the editor viewport
- [ ] Changing `button_shape.tres` corner radius updates all three buttons
- [ ] Changing `variant_danger.tres` color only affects the danger button
- [ ] Click hit testing respects the rounded shape (corners don't respond to clicks)

## Example 2: Card with Badge Cutout

**Request:** "Make a card panel with a circular hole in the top-right for a notification badge"

**Shape composition:**
```gdscript
var card := ProceduralPrimitive2D.new()
card.type = ProceduralPrimitive2D.Type.ROUNDED_RECT
card.center = Vector2(0.5, 0.5)
card.size = Vector2(1.0, 1.0)
card.corner_radii = Vector4(0.1, 0.1, 0.1, 0.1)

var hole := ProceduralPrimitive2D.new()
hole.type = ProceduralPrimitive2D.Type.CIRCLE
hole.operation = ProceduralPrimitive2D.Operation.SUBTRACT
hole.center = Vector2(0.85, 0.15)
hole.size = Vector2(0.25, 0.25)
```

**Why SUBTRACT:** The cutout is fully shape-aware — mouse clicks pass through the hole, stroke wraps around the cutout edge.

**Validation checklist:**
- [ ] Hole is visible in the editor viewport
- [ ] Stroke follows the cutout boundary (not just the outer rect)
- [ ] Clicks inside the hole pass through to nodes behind
- [ ] Total primitives ≤ 4

## Example 3: Applying Shape to Existing Sprite

**Request:** "Round the corners of an existing icon Sprite2D without replacing the node"

**Binder setup:**
```gdscript
var binder := ProceduralCanvasBinder2D.new()
binder.target_path = NodePath("../IconSprite")
binder.style = rounded_mask_style
binder.source_texture = preload("res://assets/icon.svg")
add_child(binder)
```

**Why binder:** The Sprite2D has existing positioning logic. Replacing it with ProceduralPanel2D would break that. The binder takes over the material without changing the node type.

**Validation checklist:**
- [ ] Icon renders with rounded corners
- [ ] Sprite2D positioning logic still works
- [ ] Binder's source_texture matches the sprite's original texture

## Validation Approach

### Editor-Connected

1. `get_diagnostics` on changed `.gd` files
2. Verify via editor viewport:
   - shapes render correctly at the node's current size
   - resizing the node keeps shapes proportional (UV-space coordinates)
   - boolean operations produce expected cutouts/intersections
3. Test hit testing on non-rectangular shapes:
   - clicks inside the shape register
   - clicks outside (corners, cutouts) pass through
4. Verify style sharing:
   - modifying a shared `.tres` updates all consumers
   - variants only affect their assigned nodes

### Source-Only

State what the user must verify manually:
- visual rendering in the editor viewport (shapes are `@tool` and update live)
- hit testing on non-rectangular shapes
- style sharing behavior across multiple nodes
- primitive count ≤ 4 (editor warns if exceeded)
