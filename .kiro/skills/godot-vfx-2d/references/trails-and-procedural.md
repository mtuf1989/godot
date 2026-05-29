# Trails and Procedural VFX Reference

Line2D trails, GPUParticles2D trails, custom `_draw()` trails, Tween-based procedural VFX, composition choreography, and the Tween vs AnimationPlayer vs Particles decision framework.

## Table of Contents

1. [Trail Rendering Comparison](#trail-comparison)
2. [Line2D Trails](#line2d)
3. [GPUParticles2D Trails](#particle-trails)
4. [Custom _draw() Trails](#custom-draw)
5. [Tween-Based Procedural VFX](#tween-vfx)
6. [Tween API Quick Reference](#tween-api)
7. [Tween Lifecycle and State Management](#tween-lifecycle)
8. [Tool Decision Framework](#decision-framework)
9. [Composition Choreography](#composition)

---

## 1. Trail Rendering Comparison <a name="trail-comparison"></a>

| Technique | Best For | Performance Profile | Failure Mode |
|---|---|---|---|
| **Line2D** | Few premium trails with designer iteration | `round_precision` raises cost; `antialiased = true` generates extra geometry, not accelerated by batching | Too many dynamic Line2D nodes with rounded joints/caps or AA become CPU-heavy and batch-poor |
| **GPUParticles2D trails** | Many concurrent trails (sparks, embers, comets) | Uses mesh skinning system; `trail_sections` and `trail_section_subdivisions` trade performance for smoother curves | Easy to over-smooth — too many sections or soft filtered textures make pixel-art effects look painterly |
| **Custom `_draw()`** | Pixel-locked thin lines, bespoke geometry | Commands cached until `queue_redraw()`; static geometry is cheap, fully dynamic redraws are CPU work | Polygon-based trails are unbatchable; naïve polygon strips much slower than repeated same-format primitives |

### Practical Split for Pixel Art

- **Line2D** for a few premium trails that designers iterate on constantly
- **GPUParticles2D trails** for high-concurrency ambient or combat effects
- **Custom `_draw()`** for the handful of effects that truly need pixel-locked behavior

---

## 2. Line2D Trails <a name="line2d"></a>

### Strengths

- Whole-line gradients, textured lines, caps and joints
- Clear art direction controls in the inspector
- Excellent authoring workflow for designers

### Configuration for VFX Trails

For a dynamic trail that follows a moving node, update the point array each frame:

```gdscript
extends Line2D

@export var max_points: int = 20
@export var target: Node2D

func _process(_delta: float) -> void:
    if target == null:
        return

    add_point(target.global_position)

    while get_point_count() > max_points:
        remove_point(0)
```

### Performance Notes

- `round_precision` raises render and update cost — keep low for VFX trails
- `antialiased = true` works by generating extra geometry, which can bottleneck when updated every frame
- Best for a small number of hero trails, not for dozens of concurrent streaks

---

## 3. GPUParticles2D Trails <a name="particle-trails"></a>

### Configuration

- Set `trail_enabled = true` on GPUParticles2D
- Set `trail_lifetime` — how long the trail persists behind each particle
- Configure `trail_sections` and `trail_section_subdivisions` for geometry density

### Performance

Trails use a mesh skinning system. Both `trail_sections` and `trail_section_subdivisions` explicitly trade performance for smoother curves through increased mesh complexity. For pixel art, keep sections low to avoid painterly smoothness.

### When to Use

Best for lots of concurrent trails — sparks, embers, magic streaks, swarm effects. The GPU-driven workflow scales better than CPU trail logic when many emitters are alive at once.

---

## 4. Custom _draw() Trails <a name="custom-draw"></a>

### How It Works

`_draw()` commands are cached until `queue_redraw()`. Static or event-driven geometry is cheap; fully dynamic redraws every frame are CPU work.

### Batching Rules

- **Polygon-based trails are unbatchable** — the current RD renderer cannot batch polygons, so `draw_polygon()` trail segments are a poor choice for draw-call efficiency
- **Repeated same-format `draw_primitive()` calls can batch** when texture state stays constant and point count matches
- Prefer repeated same-format primitives over polygon-per-segment approaches

### Pixel-Art Alignment

For very thin pixel-art slashes or tracer lines, odd-width `draw_line()` output needs a `0.5` offset to stay centered on the pixel grid.

### When to Use

Highest control over exact pixel placement, half-pixel alignment, stepwise motion, and bespoke behavior. The cleanest choice for 1–4 px streaks or intentionally crunchy pixel-art slashes.

---

## 5. Tween-Based Procedural VFX <a name="tween-vfx"></a>

### What It Is

Instead of spawning a particle system, animate existing nodes with code-driven property changes to create VFX-like visual feedback. A Tween chains scale pops, rotation spins, modulate flashes, and position offsets to produce effects that look like VFX but cost almost nothing compared to particles.

### Why Use Tweens for VFX

- **Cheapest option** — property animation on nodes you are already drawing, no particle allocation, no overdraw
- **Most deterministic** — exact timing, no randomness unless you add it
- **Best for pixel art** — no sub-pixel particle jitter
- **Simplest lifecycle** — fire-and-forget with automatic cleanup

### Core Architecture (Godot 4)

In Godot 4, `Tween` is a lightweight `RefCounted` object created with `Node.create_tween()`, not a scene node. `Tween.new()` is invalid for actual tweening. `Node.create_tween()` binds the tween to the calling node and starts it on the next process or physics frame. Tweens are not designed to be reused — create a fresh one each time.

### Common VFX Patterns

**Scale pop (squash & stretch):**

```gdscript
func juice_pop(node: Node2D) -> void:
    var t := node.create_tween().set_process_mode(Tween.TWEEN_PROCESS_PHYSICS)

    t.tween_property(node, "scale", Vector2(1.25, 0.80), 0.04) \
        .set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)

    t.tween_property(node, "scale", Vector2(0.92, 1.08), 0.05) \
        .set_trans(Tween.TRANS_ELASTIC).set_ease(Tween.EASE_OUT)

    t.tween_property(node, "scale", Vector2.ONE, 0.07) \
        .set_trans(Tween.TRANS_BOUNCE).set_ease(Tween.EASE_OUT)
```

**Hit shake (deterministic):**

```gdscript
func hit_shake(node: Node2D) -> void:
    var origin := node.position
    var t := node.create_tween().set_process_mode(Tween.TWEEN_PROCESS_PHYSICS)

    t.tween_property(node, "position", origin + Vector2(8, 0), 0.016)
    t.tween_property(node, "position", origin + Vector2(-6, 0), 0.016)
    t.tween_property(node, "position", origin + Vector2(4, 0), 0.016)
    t.tween_property(node, "position", origin, 0.024)
```

**Fade with child safety (self_modulate):**

```gdscript
func child_safe_fade(item: CanvasItem) -> void:
    item.self_modulate = Color(1, 1, 1, 1)
    var t := item.create_tween()
    t.tween_property(item, "self_modulate", Color(1.0, 0.85, 0.85, 1.0), 0.03)
    t.tween_property(item, "self_modulate:a", 0.0, 0.12)
```

**Float up (relative offset):**

```gdscript
func float_up(node: Node2D) -> void:
    node.create_tween() \
        .tween_property(node, "position:y", -24.0, 0.20) \
        .as_relative()
```

**Shader uniform pulse:**

```gdscript
func pulse_uniform(item: CanvasItem, uniform_name: StringName = "flash_strength") -> void:
    var mat := item.material as ShaderMaterial
    if mat == null:
        return

    var t := item.create_tween()
    t.tween_method(
        mat.set_shader_parameter.bind(uniform_name),
        0.0, 1.0, 0.05
    ).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)

    t.tween_method(
        mat.set_shader_parameter.bind(uniform_name),
        1.0, 0.0, 0.08
    ).set_trans(Tween.TRANS_BOUNCE).set_ease(Tween.EASE_OUT)
```

**Ephemeral VFX node (spawn → animate → free):**

```gdscript
func spawn_hit_puff(parent: Node, texture: Texture2D, local_pos: Vector2) -> void:
    var puff := Sprite2D.new()
    puff.texture = texture
    puff.position = local_pos
    puff.scale = Vector2(0.70, 0.70)
    puff.modulate = Color(1, 1, 1, 0.95)
    parent.add_child(puff)

    var t := puff.create_tween().set_process_mode(Tween.TWEEN_PROCESS_PHYSICS)
    t.tween_property(puff, "scale", Vector2(1.25, 1.25), 0.06) \
        .set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
    t.parallel().tween_property(puff, "modulate:a", 0.0, 0.12)
    t.chain().tween_callback(puff.queue_free)
```

### Game-Feel Easing Guide

| Transition + Ease | Feel | Best For |
|---|---|---|
| `TRANS_BACK` + `EASE_OUT` | Quick anticipation and punch | Scale pops, recoil snaps |
| `TRANS_ELASTIC` + `EASE_OUT` | Springy, magical, rubbery | Bouncy UI, magical effects |
| `TRANS_BOUNCE` + `EASE_OUT` | Hard-settle impact | Landing squash, damage numbers |
| `TRANS_QUAD` + `EASE_OUT` | Smooth deceleration | Fades, drifts |
| `TRANS_LINEAR` | Constant speed | Scrolling, uniform motion |

### Properties Commonly Tweened for VFX

| Property | Type | VFX Use |
|----------|------|---------|
| `scale` | Vector2 | Pop, squash/stretch, grow/shrink |
| `rotation` | float | Spin, wobble |
| `modulate` | Color | Flash white, fade alpha, tint shift |
| `self_modulate` | Color | Same but doesn't affect children |
| `position` | Vector2 | Float up, shake, drift |
| `position:y` | float | Vertical drift via sub-property |
| `modulate:a` | float | Alpha-only fade |
| `material:shader_parameter/name` | varies | Animate shader uniforms (use `tween_method` for reliability) |

---

## 6. Tween API Quick Reference <a name="tween-api"></a>

### Four Core Workers

| Worker | Method | VFX Job |
|--------|--------|---------|
| `PropertyTweener` | `tween_property()` | Scale, position, rotation, alpha, modulate |
| `CallbackTweener` | `tween_callback()` | `queue_free()`, visibility toggles, sound hooks, spawn next phase |
| `IntervalTweener` | `tween_interval()` | Anticipation windows, holds, cadence gaps |
| `MethodTweener` | `tween_method()` | Shader uniform pulses, trauma values, custom draw variables |

### Composition

- Default is **sequential** — tweeners execute one after another
- `.parallel()` on the next step makes it run alongside the previous
- `.chain()` forces sequential after a parallel block
- `set_parallel(true)` makes all subsequent steps parallel until `.chain()`
- `set_loops()` repeats the whole sequence; no arguments = infinite loop
- `as_relative()` offsets from current value instead of absolute target
- `from()` / `from_current()` sets explicit start value

### Sub-Property Paths

`tween_property()` accepts NodePath with colon-separated sub-properties: `"position:x"`, `"modulate:a"`, `"scale:y"`. This avoids tweening the full Vector2/Color when only one component matters.

---

## 7. Tween Lifecycle and State Management <a name="tween-lifecycle"></a>

### Ownership and Cleanup

- `Node.create_tween()` binds the tween to the node — halted when node leaves tree, killed when node is freed
- `PropertyTweener`, `CallbackTweener`, and `MethodTweener` auto-finish if their target object is freed
- Tweens are processed by the SceneTree independently of animated nodes

### Critical Rules

1. **One writer per property per effect phase.** Animating the same property with multiple tweens simultaneously causes jitter — the last created tween wins.
2. **Tweens are processed after `_process()` / `_physics_process()`.** Mixing manual property writes and tween writes to the same property causes snapping.
3. **`kill()` does not restore property values.** Store baseline values yourself and restore on cancel.
4. **`stop()` resets to initial state without removing tweeners.** Paused/stopped unbound tweens persist indefinitely.

### Robust Cancel Pattern

```gdscript
var active_fx_tween: Tween
var base_scale := Vector2.ONE
var base_modulate := Color.WHITE

func begin_fx(node: Node2D) -> void:
    cancel_fx(node)
    base_scale = node.scale
    base_modulate = node.modulate

    active_fx_tween = node.create_tween()
    active_fx_tween.tween_property(node, "scale", Vector2(1.15, 0.90), 0.04)
    active_fx_tween.tween_property(node, "scale", base_scale, 0.06)

func cancel_fx(node: Node2D) -> void:
    if active_fx_tween and active_fx_tween.is_valid():
        active_fx_tween.kill()
    node.scale = base_scale
    node.modulate = base_modulate
    active_fx_tween = null
```

### Shader Material Warning

Changing a shader uniform on a ShaderMaterial affects all instances using that shared material. For per-entity procedural VFX, either give each node its own material instance or deliberately accept broadcast behavior. `tween_method()` with `set_shader_parameter` is the safest baseline for shader uniform pulses.

---

## 8. Tool Decision Framework <a name="decision-framework"></a>

### Tween vs AnimationPlayer vs Particles

| Tool | Best Fit | Switch When |
|---|---|---|
| **Tween** | Short runtime-derived property effects on ≤3 nodes, ≤6 properties, ≤8 beats | Effect exceeds 2 of those limits, or designer needs visual timeline editing |
| **AnimationPlayer** | Reusable authored timelines, non-trivial timing, designer-tuned sequences | Effect is fundamentally "emit and simulate lots of short-lived things" |
| **GPUParticles2D** | Many independently moving elements with lifetime, randomness, trails, flipbooks | N/A — this is the ceiling tool |

### Practical Thresholds

- **Tween → AnimationPlayer**: When the effect is no longer naturally read as a small chain of property edits in code. If you maintain several parallel groups, many callbacks just to keep time, or a long irregular timing graph that an artist cannot tune from script, switch to AnimationPlayer.
- **Tween/AnimationPlayer → Particles**: When the picture in your head is "spawn lots of short-lived things that each have their own lifetime, trajectory, and variation." Roughly **32+ simultaneously alive independently moving elements** is where particles become the objectively correct abstraction.

### Performance Note

Tweened node VFX is not free — you still pay to draw every visible CanvasItem, and 2D batching only helps when items remain similar enough to batch. But a handful of tweens on existing sprites is a very different cost profile from a transparency-heavy particle cloud where fill rate and overlap dominate.

---

## 9. Composition Choreography <a name="composition"></a>

### Combining Multiple VFX Tools

Complex 2D effects often combine particles, trails, screen effects, and Tween animation. The recommended approach:

**Trigger mechanisms (choose one per effect):**
- **Script** — for gameplay-driven timing, call methods directly or via signals
- **AnimationPlayer call tracks** — for authored timelines with precise frame-level control
- **Tween chain with `tween_callback()`** — for simple sequential compositions

**Example: Sword slash composition**

```gdscript
func play_slash(origin: Vector2) -> void:
    # 1. Spawn slash sprite with Tween animation
    var slash := Sprite2D.new()
    slash.texture = slash_texture
    slash.position = origin
    slash.scale = Vector2(0.5, 0.5)
    slash.modulate.a = 0.9
    add_child(slash)

    var t := slash.create_tween()
    t.tween_property(slash, "scale", Vector2(1.3, 1.3), 0.08) \
        .set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
    t.parallel().tween_property(slash, "modulate:a", 0.0, 0.15)
    t.chain().tween_callback(slash.queue_free)

    # 2. Trigger particle burst
    $SparkParticles.position = origin
    $SparkParticles.restart()

    # 3. Trigger screen flash (on dedicated CanvasLayer)
    $ScreenFlash/FlashRect.modulate.a = 0.6
    var flash_t := $ScreenFlash/FlashRect.create_tween()
    flash_t.tween_property($ScreenFlash/FlashRect, "modulate:a", 0.0, 0.08)
```

### Cleanup Rules

- Ephemeral nodes: `queue_free()` via `tween_callback()` or the `finished` signal
- Particle systems: use `one_shot = true` for bursts; they auto-stop after one emission cycle
- Screen effects: tween intensity/alpha back to 0, then hide or disable the CanvasLayer
- CanvasGroup wrappers: if the group is permanent, just tween children; if ephemeral, free the whole group
