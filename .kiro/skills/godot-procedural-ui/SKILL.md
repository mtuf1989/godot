---
name: godot-procedural-ui
description: |
  Build resolution-independent UI shapes in Godot 4 using the Procedural Shader Utility addon —
  SDF-based rounded rects, circles, capsules, boolean cutouts, gradient fills, strokes, and
  shared style/variant systems for buttons, cards, panels, badges, and other UI elements.
  Use when the user needs crisp procedural UI shapes without texture assets — rounded buttons,
  pill-shaped tags, cards with cutouts, gradient-stroked panels, or a reusable style library
  with color variants. Also trigger when the user mentions ProceduralPanel2D,
  ProceduralCanvasBinder2D, ProceduralStyle2D, ProceduralShape2D, ProceduralPrimitive2D,
  ProceduralPaint2D, ProceduralStyleVariant2D, SDF shapes, procedural UI, "no-texture UI,"
  or asks for resolution-independent UI rendering.
  Even if the user just says "I need a rounded button without importing images" or
  "make a pill-shaped tag" or "card with a circular cutout," this skill applies.
  Do NOT use for hand-written canvas_item shader effects like hit flash, dissolve, outline,
  palette swap, or UV distortion — those belong to godot-shader-canvasitem-fx.
---

# Godot Procedural UI

Compose crisp, resolution-independent UI shapes using the Procedural Shader Utility addon's resource system — no hand-written shaders, no texture imports.

Read `references/shape-and-style-catalog.md` first. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the actual scene and UI layout the procedural shape will live in before editing.
- Compose shapes from SDF primitives (rounded rect, sharp rect, circle, capsule) with boolean operations (union, subtract, intersect).
- Configure paint (solid/gradient fill, solid/gradient stroke, feather) appropriate to the design.
- Build shared style libraries with `.tres` resources and lightweight variants for color/state changes.
- Use `ProceduralPanel2D` for new UI elements and `ProceduralCanvasBinder2D` when applying shapes to existing nodes.
- Use native file editing for scripts and `.tres` resources, then MCP tools for scene assembly when editor-connected.
- Validate with the strongest available method and report limits honestly.

## Use When

- The task is building UI elements from procedural shapes — buttons, cards, panels, badges, chips, tabs, chat bubbles.
- The user wants resolution-independent UI without importing texture assets.
- A shared style/variant system is needed for consistent UI theming.
- Boolean shape operations (cutouts, intersections) are needed for UI elements.
- Shape-aware hit testing is needed (clicks respect the actual shape, not the bounding box).
- An existing node needs a procedural shape applied via `ProceduralCanvasBinder2D`.

## Do Not Use When

- The task is a hand-written shader effect (hit flash, dissolve, outline, palette swap, UV warp, screen transition) — route to `godot-shader-canvasitem-fx`.
- The task is UI layout, containers, or menu flow without procedural shapes — route to `godot-ui-core`.
- The task is general GDScript unrelated to procedural shapes — route to `godot-gdscript`.
- Scene/resource ownership of style `.tres` files is the main concern — route to `godot-scene-resource`.
- Architecture decisions about whether to use procedural shapes at all are still open — route to `godot-architect`.

## Core Mental Model

The addon has a layered resource system:

```
ProceduralPrimitive2D   — one SDF shape (ROUNDED_RECT, RECT, CIRCLE, CAPSULE)
        ↓
ProceduralShape2D       — primitives composed with boolean ops (max 4)
        ↓
ProceduralPaint2D       — fill + stroke + feather + gradients
        ↓
ProceduralStyle2D       — shape + paint bundled
        ↓  optionally overridden by
ProceduralStyleVariant2D — shallow paint overrides
        ↓  fed to
ProceduralPanel2D       — Control node that renders the style (has shape-aware hit testing)
ProceduralCanvasBinder2D — applies style to existing node (NO hit testing)
```

All shape coordinates are in normalized UV space (0–1). Stroke and feather are in pixels. Everything is `@tool` and updates live in the editor.

### Override Precedence Chain

```
base ProceduralStyle2D
  → ProceduralStyleVariant2D overrides (if _override bool is true)
    → Instance overrides (on the panel/binder node)
      → Compiler output (final)
```

Later in the chain always wins. Instance overrides are per-node one-off tweaks exposed directly on ProceduralPanel2D and ProceduralCanvasBinder2D under the "Instance Overrides" inspector group.

## Workflow

1. Read `references/shape-and-style-catalog.md`, then inspect the project surfaces:
   - the scene and layout context where the shape will live
   - existing `ProceduralPanel2D` nodes and shared style `.tres` files (use `find_nodes` with type "ProceduralPanel2D" for targeted lookups)
   - whether the target is a new UI element or an existing node needing a binder
   - the project's UI theme conventions (colors, corner radii, stroke widths)
   In editor-connected mode, use `search_symbols` or `get_hover_info` to resolve addon API questions.

2. Compose the shape:
   - pick primitive types that match the design (ROUNDED_RECT for cards/buttons, CAPSULE for pills/tags, CIRCLE for badges, RECT for sharp-edged dividers/containers)
   - RECT ignores `corner_radii` entirely — use ROUNDED_RECT if you need any rounding
   - CIRCLE forces equal dimensions using `min(w, h)` — it cannot produce ellipses; use ROUNDED_RECT with large corner radii for ellipses
   - use boolean subtract for cutouts, intersect for overlap regions
   - set `smoothness_px` on a primitive to blend its boolean operation with a rounded transition (0.0 = hard edge, higher values = softer blend). Useful for organic shapes, melted unions, and soft cutouts. The value is in pixels and converted to local scale by the compiler using the same `min(w,h)` divisor as stroke/feather
   - boolean operations evaluate strictly top-to-bottom; the first primitive's operation and smoothness are ignored (it initializes the SDF)
   - stay within the 4-primitive budget — extras are silently dropped
   - set `corner_radii` per-corner for asymmetric shapes (tabs, chat bubbles); radii are clamped to half the shortest side automatically

3. Configure the paint:
   - solid fill for simple elements, gradient fill for styled surfaces
   - stroke for borders — solid or gradient, width in pixels
   - `feather_px >= 1.0` recommended for anti-aliased edges. When feather is 0, the shader falls back to `fwidth(dist)` for screen-space AA — this works but may produce thinner or inconsistent edges at different zoom levels
   - gradient needs a `Gradient` or `GradientTexture1D` resource assigned; setting gradient mode without one falls back to no fill (silent at runtime, editor warning shown)
   - each gradient (fill and stroke independently) has transform fields: `offset` (normalized), `rotation_deg`, `scale`
   - `Gradient` resources are auto-wrapped in `GradientTexture1D` with caching; `GradientTexture1D` is passed through unchanged
   - `stroke_width_px` and `feather_px` are divided by `min(target_width, target_height)` — a 2px stroke looks proportionally different on different-sized panels
   - texture modulation: set `source_texture` on ProceduralPanel2D or ProceduralCanvasBinder2D to multiply fill color by a texture (e.g., rounded-corner masking on an icon). This is a backend-only feature, not part of ProceduralPaint2D.

4. Build the style library when reuse matters:
   - save `ProceduralStyle2D` as `.tres` files — one style, many consumers
   - create `ProceduralStyleVariant2D` for color/state changes (hover, danger, disabled)
   - use instance overrides only for one-off per-node tweaks
   - variants override paint properties only, not shape structure

5. Choose the right node:
   - `ProceduralPanel2D` for new UI elements (participates in layout, has shape-aware hit testing — clicks register inside fill + stroke + feather boundary with 1px tolerance)
   - `ProceduralCanvasBinder2D` for applying shapes to existing nodes you can't replace — does NOT provide hit testing; the target keeps its own `_has_point()` behavior

6. In editor-connected mode, call `refresh_filesystem` after each changed `.tres` or `.gd` file.

7. Run the strongest available validation:
   - editor-connected: verify shape renders in viewport, check hit testing on non-rectangular shapes, confirm style sharing works across multiple panels
   - source-only: state the exact editor validation step still required

8. Escalate if needed:
   - hand-written shader effects → `godot-shader-canvasitem-fx`
   - UI layout and container structure → `godot-ui-core`
   - style `.tres` ownership issues → `godot-scene-resource`
   - whether to use procedural shapes at all → `godot-architect`
   - general GDScript → `godot-gdscript`

## Composition Defaults

Unless the task states otherwise:

- `feather_px = 1.0` minimum on all shapes
- `corner_radii = Vector4(0.08, 0.08, 0.08, 0.08)` for subtle rounding
- stroke width 1–3px for borders
- save styles as `.tres` when used by more than one node
- use variants instead of duplicating styles for color changes
- `ProceduralPanel2D` over binder for new UI elements

## Output

Produce bounded procedural UI work that includes:

- shape composition (primitives, boolean ops, paint settings)
- style library structure if shared styles were created
- variant definitions if state/color variants were needed
- node choice rationale (Panel2D vs CanvasBinder2D)
- MCP sync or LSP steps used when editor-connected
- validation performed and its limits
- exact blocker and next action if blocked

## Quality Gates

- Shapes stay within the 4-primitive budget.
- Feather is set for anti-aliased edges.
- Shared styles use `.tres` files, not inline duplicates.
- Color/state changes use variants, not duplicated styles.
- `ProceduralPanel2D` is used for new UI elements that need layout participation and hit testing.
- `ProceduralCanvasBinder2D` is used only when applying to existing nodes.
- The implementation is grounded in actual project artifacts.

## Performance Awareness

### Cheap Operations
- Changing paint properties (colors, stroke width, feather) only updates shader uniforms — near-zero cost.
- Sharing styles across many nodes shares the shader resource; only material instances are unique.
- Resizing triggers lightweight recompilation (pixel-to-local conversion).

### Not-Free Operations
- Adding/removing primitives triggers full recompilation — hundreds of microseconds.
- First use of a `Gradient` resource allocates a `GradientTexture1D` (cached afterward, pruned when source is freed).

### Draw Calls and Overdraw
- Each ProceduralPanel2D is a separate draw call because each has unique shader uniform values (material instance uniqueness breaks batching even when styles are shared).
- Godot 2D has no depth buffer — overlapping transparent panels pay full fragment cost per layer. The SDF evaluation (loop over 4 primitives + boolean ops + gradient sampling) runs for every pixel of every overlapping panel.
- Gradient textures with different filter settings split batches further. Keep gradient filter modes consistent across panels in the same UI layer.
- For UIs with 100+ visible procedural panels, profile draw call count via the Godot profiler.

### Instantiation Hitch
- Having many idle ProceduralPanel2D nodes is cheap (~10µs/frame per 1000 nodes). But instantiating many simultaneously (e.g., populating an inventory grid) causes a one-time hitch from node creation + initial compilation. For grids with 50+ identical procedural panels, stagger instantiation across frames or pre-instantiate off-screen.

### Baking Static Shapes
- For static shapes that never change at runtime, render a ProceduralPanel2D into a SubViewport with `update_mode = UPDATE_ONCE`, then use the resulting ViewportTexture on a TextureRect. This eliminates per-frame SDF evaluation for that shape.

### HDR 2D for Gradient Quality
- If gradient fills show visible banding on large panels with subtle color transitions, enable `Viewport.use_hdr_2d` to switch 2D rendering to RGBA16 linear space. This is a project-level setting, not an addon setting.

## Animating Procedural UI

Paint properties are shader uniforms — animating them via tweens is cheap and avoids recompilation. Shape structure changes (adding/removing primitives) trigger recompilation and should not be tweened.

### Tween Pattern

```gdscript
var _fx_tween: Tween

func _hover_enter() -> void:
    if _fx_tween: _fx_tween.kill()
    _fx_tween = create_tween()
    _fx_tween.tween_method(_set_stroke, 1.0, 3.0, 0.15) \
        .set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)

func _set_stroke(val: float) -> void:
    $Panel.material.set_shader_parameter("u_stroke_width_px", val / min($Panel.size.x, $Panel.size.y))
```

Always kill the previous tween before starting a new one — overlapping tweens on the same uniform fight and produce jitter. Store one `Tween` reference per animated property group.

### Animatable Properties

| Property | Uniform | Good for |
|----------|---------|----------|
| Fill color | `u_fill_color` | Hover tint, press darken, disabled grey |
| Stroke color | `u_stroke_color` | Focus ring color |
| Stroke width | `u_stroke_width_px` | Focus ring pop (needs `/ min(w,h)` conversion) |
| Feather | `u_feather_px` | Soft glow pulse (needs `/ min(w,h)` conversion) |
| Gradient offset/rotation/scale | `u_fill_gradient_*` | Animated gradient sweep |
| Smoothness | `u_shape_smoothness[i]` | Morphing between hard and soft booleans |

Pixel-space uniforms (`stroke_width_px`, `feather_px`) are stored in normalized form after compilation. When tweening them directly on the material, divide by `min(size.x, size.y)` yourself.

### Curve Recommendations

- `TRANS_BACK + EASE_OUT` — button press pop (stroke width, scale)
- `TRANS_CUBIC + EASE_OUT` — smooth hover color transitions
- `TRANS_ELASTIC + EASE_OUT` — playful badge/notification bounce
- `TRANS_LINEAR` — progress bar fills, loading indicators

### self_modulate vs modulate

When a ProceduralPanel2D has child nodes (labels, icons), `modulate` tints the panel AND all children. Use `self_modulate` to tint only the procedural shape while leaving children unaffected. Example: dimming a button background on disable without greying out the label.

### CanvasGroup for Composite Fading

When multiple overlapping ProceduralPanel2D nodes need to fade as a unit (e.g., a card with a badge overlay), wrapping them in a `CanvasGroup` prevents double-blending artifacts. Without CanvasGroup, the overlap region becomes more opaque than intended when reducing `modulate.a`.

## Editor Warnings

The addon surfaces configuration issues as yellow-triangle warnings in the Scene dock. These are not errors — the addon never crashes on bad input. Key warnings: "no style assigned", "shape/paint resource is null", "Shape has N primitives, backend renders 4", "fill/stroke mode is GRADIENT but no gradient assigned", "Target size is non-positive", "CIRCLE has unequal size", "corner radii clamped". Use these warnings as validation signals during development.

## Failure Modes

- Do not hand-write `.gdshader` files — the addon generates shaders internally. If a hand-written shader is needed, escalate to `godot-shader-canvasitem-fx`.
- Do not exceed 4 primitives per shape — extras are silently dropped (with editor warning). The visual result may look correct if dropped primitives were small — always check the warning.
- Do not duplicate styles when variants or instance overrides would work.
- Do not skip feather — edges look jagged without it.
- Do not use `ProceduralCanvasBinder2D` when `ProceduralPanel2D` would be simpler and provide hit testing.
- Do not try to override shape structure through variants — variants only change paint properties.
- Do not make architecture decisions during implementation — escalate to `godot-architect`.
- Boolean operations evaluate strictly top-to-bottom. Reordering primitives with SUBTRACT produces different shapes. The first primitive's operation and smoothness are always ignored (it initializes the SDF).
- Smooth boolean ops (`smoothness_px > 0`) slightly alter the shape boundary. Smooth union expands the surface at the junction; smooth subtract rounds the carve edge. Hit testing uses the same smooth SDF, so clicks match the visual boundary. Large `smoothness_px` values on small panels may produce unexpected shapes — preview in the editor.
- `RECT` ignores `corner_radii` entirely. Use `ROUNDED_RECT` if you need any rounding.
- `CIRCLE` forces equal dimensions using `min(w, h)`. It cannot produce ellipses — use `ROUNDED_RECT` with large corner radii instead.
- `stroke_width_px` and `feather_px` are divided by `min(target_width, target_height)`. A 2px stroke on a 100×100 panel looks proportionally different than on a 400×400 panel.
- Setting gradient mode without assigning a gradient resource falls back to no fill (equivalent to NONE). Silent at runtime, editor warning shown.
- If a panel or binder target has zero width or height, pixel-to-local conversion is skipped — stroke and feather become 0.0 and nothing renders.
- Enum values (`Type`, `Operation`, `PaintMode`, `GradientType`) are serialization-sensitive. If forking the addon, only append new values — reordering breaks saved `.tres` resources.
- If subclassing any resource, call `emit_changed()` in custom setters or live updates break silently.
- Do not mutate a shared `.tres` style at runtime (e.g., changing `style.paint.fill_color` in a hover script). Shared resources are global state — every panel using that style will change. Use instance overrides or variants for per-node runtime changes.
- In Godot 4.6+, `resource_local_to_scene` and `duplicate(true)` correctly recurse into arrays/dictionaries. For ProceduralShape2D primitives stored in `Array[ProceduralPrimitive2D]`, use `duplicate_deep(Resource.DEEP_DUPLICATE_INTERNAL)` when you need a fully isolated copy. On pre-4.6 engines, resources inside arrays were NOT duplicated — use explicit per-element duplication as a fallback.
- If targeting multiple engine versions, prefer explicit duplication or instance overrides to guarantee per-instance isolation regardless of engine version.

## Shader Internals

These details help explain addon behavior when debugging or answering user questions. Do not modify the shader directly — the addon owns it.

### fwidth() Anti-Aliasing Fallback
The shader computes `float aa = max(feather, fwidth(dist))`. When `feather_px` is 0, `fwidth(dist)` provides screen-space-adaptive anti-aliasing automatically — edges stay smooth at any zoom level. Setting `feather_px >= 1.0` is still recommended because it gives explicit control over edge softness and produces more consistent results across different panel sizes.

### Single-File Shader by Design
The addon uses one file-backed `.gdshader` with uniform-driven branching (not runtime shader string generation). This is deliberate — file-backed shaders benefit from Godot's shader precompilation and pipeline caching. Do not generate shader code dynamically (e.g., to support more than 4 primitives via variable-length loops) as this defeats precompilation and causes runtime stalls on first use.

### Recompilation Loop Risk
The addon's `@tool` scripts recompile when any composed resource emits `changed`. If a custom script listens to a style's `changed` signal and modifies a style property in response, it creates an infinite recompilation loop that freezes the editor. Guard such scripts with a `_is_updating` flag or use `call_deferred`.

### Aspect Ratio and Pixel Conversion
`stroke_width_px` and `feather_px` are divided by `min(target_width, target_height)` for normalization. On highly elongated panels (e.g., 100×400), stroke and feather are normalized by the short side (100), making them appear proportionally thicker along the long axis. This is by design for uniform visual weight but may surprise users expecting pixel-perfect stroke width on non-square panels.

## References

Read only as needed:

- `references/shape-and-style-catalog.md`
- `references/examples-and-validation.md`
- `../../foundation/Godot Nuanced Development Practices.md`
