# Examples and Validation

Worked examples and validation checklists for animation systems. Read after `animation-core.md` / `animation-2d.md` / `animation-3d.md`.

## Table of Contents

1. [2D Platformer Character Animation](#2d-platformer-character-animation)
2. [Combat Animation with Hitboxes](#combat-animation-with-hitboxes)
3. [3D Character with Blend Tree](#3d-character-with-blend-tree)
4. [Procedural Tail (Verlet Chain)](#procedural-tail-verlet-chain)
5. [Validation Checklist](#validation-checklist)

---

## 2D Platformer Character Animation

AnimatedSprite2D with idle/run/jump/fall, driven by an AnimationController that reads CharacterBody2D state.

### Scene Structure

```
Player (CharacterBody2D)
├── AnimatedSprite2D        ← SpriteFrames: idle(loop), run(loop), jump, fall(loop)
├── CollisionShape2D
└── PlayerAnimationController.gd  ← attached to Player
```

### SpriteFrames Setup

| Animation | Frames | FPS | Loop |
|-----------|--------|-----|------|
| idle      | 4      | 8   | ✓    |
| run       | 6      | 12  | ✓    |
| jump      | 2      | 10  | ✗    |
| fall      | 2      | 10  | ✓    |

### AnimationController

```gdscript
class_name PlayerAnimationController
extends Node

@onready var _body: CharacterBody2D = owner as CharacterBody2D
@onready var _sprite: AnimatedSprite2D = %AnimatedSprite2D

var _current_anim: StringName = &""

func _physics_process(_delta: float) -> void:
    var anim := _resolve_animation()
    _update_flip()
    if anim != _current_anim:
        var prev := _current_anim
        _current_anim = anim
        _sprite.play(anim)
        GLog.transition("Player", prev, anim, "anim_controller",
            {"velocity_y": _body.velocity.y})

func _resolve_animation() -> StringName:
    if not _body.is_on_floor():
        return &"fall" if _body.velocity.y > 0.0 else &"jump"
    if absf(_body.velocity.x) > 10.0:
        return &"run"
    return &"idle"

func _update_flip() -> void:
    if not is_zero_approx(_body.velocity.x):
        _sprite.flip_h = _body.velocity.x < 0.0
```

**Key points:**
- `_resolve_animation()` is a pure query — no side effects.
- `play()` is only called when the animation actually changes, avoiding restart-per-frame.
- `flip_h` is set from velocity direction; for input-facing, replace with `_body.get_last_input_direction()`.
- `GLog.transition()` logs every state change with velocity context.

### GdUnit4 Test

```gdscript
class_name PlayerAnimationControllerTest
extends GdUnitTestSuite

const __source = "res://scripts/player/player_animation_controller.gd"

func test_idle_on_ground() -> void:
    var runner := scene_runner("res://scenes/player/player.tscn")
    runner.set_time_factor(9.0)
    # Let physics settle on ground
    await runner.simulate_frames(10)
    var anim: StringName = runner.get_property("_current_anim")
    assert_str(anim).is_equal("idle")

func test_fall_when_airborne() -> void:
    var runner := scene_runner("res://scenes/player/player.tscn")
    runner.set_time_factor(9.0)
    # Move player above ground
    runner.invoke("set_position", Vector2(100, -200))
    await runner.simulate_frames(5)
    var anim: StringName = runner.get_property("_current_anim")
    assert_str(anim).is_equal("fall")
```

---

## Combat Animation with Hitboxes

AnimationPlayer with method tracks for hitbox enable/disable, cancel windows, input buffering for combo queuing, and an AnimationTree state machine for the combo chain.

### Scene Structure

```
Fighter (CharacterBody2D)
├── Sprite2D
├── AnimationPlayer          ← clips: idle, attack1, attack2, attack3
├── AnimationTree            ← state machine: idle → attack1 → attack2 → attack3
├── HitboxPivot
│   └── Hitbox (Area2D)
│       └── CollisionShape2D ← disabled by default
└── FighterAnimationController.gd
```

### AnimationPlayer Method Tracks

Each attack clip includes method call tracks on the AnimationController:

| Track Target | Time | Method | Args |
|---|---|---|---|
| `..` (controller) | 0.1s | `_enable_hitbox` | `"attack1"` |
| `..` (controller) | 0.25s | `_disable_hitbox` | — |
| `..` (controller) | 0.2s | `_open_cancel_window` | — |
| `..` (controller) | 0.4s | `_close_cancel_window` | — |

Set `callback_mode_method = IMMEDIATE` on the AnimationMixer for frame-precise hitbox timing.

### AnimationController

```gdscript
class_name FighterAnimationController
extends Node

signal combo_advanced(attack_name: StringName)

@onready var _tree: AnimationTree = %AnimationTree
@onready var _sm: AnimationNodeStateMachinePlayback = _tree.get(
    "parameters/playback") as AnimationNodeStateMachinePlayback
@onready var _hitbox_shape: CollisionShape2D = %Hitbox.get_child(0)

const COMBO_CHAIN: Array[StringName] = [&"attack1", &"attack2", &"attack3"]

var _current_attack_idx: int = -1
var _cancel_window_open: bool = false
var _input_buffered: bool = false

func _physics_process(_delta: float) -> void:
    if _input_buffered and _cancel_window_open:
        _advance_combo()
        _input_buffered = false

func request_attack() -> void:
    if _current_attack_idx < 0:
        _current_attack_idx = 0
        _sm.travel(COMBO_CHAIN[0])
        GLog.info("combo_start", "animation", {"attack": COMBO_CHAIN[0]})
    elif _cancel_window_open:
        _advance_combo()
    else:
        _input_buffered = true
        GLog.debug("input_buffered", "animation",
            {"for_attack_idx": _current_attack_idx + 1})

func _advance_combo() -> void:
    _cancel_window_open = false
    _current_attack_idx += 1
    if _current_attack_idx >= COMBO_CHAIN.size():
        _end_combo()
        return
    var attack := COMBO_CHAIN[_current_attack_idx]
    _sm.travel(attack)
    combo_advanced.emit(attack)
    GLog.transition("Fighter", COMBO_CHAIN[_current_attack_idx - 1],
        attack, "combo_advance")

func _end_combo() -> void:
    _current_attack_idx = -1
    _cancel_window_open = false
    _input_buffered = false
    _sm.travel(&"idle")
    GLog.info("combo_end", "animation", {})

# Called by AnimationPlayer method tracks
func _enable_hitbox(attack_name: String) -> void:
    _hitbox_shape.disabled = false
    GLog.info("hitbox_activated", "animation", {"attack": attack_name})

func _disable_hitbox() -> void:
    _hitbox_shape.disabled = true

func _open_cancel_window() -> void:
    _cancel_window_open = true

func _close_cancel_window() -> void:
    if _input_buffered:
        _advance_combo()
    _cancel_window_open = false
    _input_buffered = false

# Called by AnimationTree when returning to idle (connect animation_finished)
func _on_animation_finished(anim_name: StringName) -> void:
    if anim_name in COMBO_CHAIN:
        _end_combo()
```

**Key points:**
- Input buffer captures attack presses outside the cancel window and replays them when the window opens.
- Cancel window is opened/closed by method tracks — frame-precise timing authored in the animation.
- `callback_mode_method = IMMEDIATE` ensures hitbox enable/disable happens on the exact physics frame.
- Combo resets to idle if the chain completes or the cancel window closes without input.

### GdUnit4 Test

```gdscript
class_name FighterAnimationControllerTest
extends GdUnitTestSuite

const __source = "res://scripts/fighter/fighter_animation_controller.gd"

func test_combo_chain_advances() -> void:
    var runner := scene_runner("res://scenes/fighter/fighter.tscn")
    runner.set_time_factor(9.0)
    var ctrl := runner.get_property("_tree").get_parent()
    monitor_signals(ctrl)
    # Start combo
    runner.invoke("request_attack")
    await runner.simulate_frames(20)
    # Buffer next attack during cancel window
    runner.invoke("request_attack")
    await runner.simulate_frames(20)
    await assert_signal(ctrl).is_emitted("combo_advanced")

func test_hitbox_disabled_by_default() -> void:
    var runner := scene_runner("res://scenes/fighter/fighter.tscn")
    await runner.simulate_frames(2)
    var disabled: bool = runner.get_property("_hitbox_shape").disabled
    assert_bool(disabled).is_true()
```

---

## 3D Character with Blend Tree

AnimationTree with BlendSpace1D for locomotion (idle → walk → run), OneShot for attack overlay, and root motion extraction.

### Scene Structure

```
Character3D (CharacterBody3D)
├── CharacterModel (Node3D)
│   ├── Skeleton3D
│   │   └── AnimationPlayer    ← clips: idle, walk, run, attack
│   └── MeshInstance3D
├── AnimationTree              ← blend tree root
└── Character3DAnimationController.gd
```

### AnimationTree Graph

```
BlendTree (root)
├── BlendSpace1D "locomotion"
│   ├── 0.0 → idle
│   ├── 0.5 → walk
│   └── 1.0 → run
├── OneShot "attack_oneshot"
│   ├── in → locomotion output
│   └── shot → attack
└── Output ← attack_oneshot output
```

Set `root_motion_track` on AnimationTree to the root bone path for root motion extraction.

### AnimationController

```gdscript
class_name Character3DAnimationController
extends Node

@onready var _body: CharacterBody3D = owner as CharacterBody3D
@onready var _tree: AnimationTree = %AnimationTree

const MAX_SPEED: float = 6.0

var _prev_blend: float = 0.0

func _physics_process(delta: float) -> void:
    _update_locomotion(delta)
    _apply_root_motion(delta)

func _update_locomotion(_delta: float) -> void:
    var speed := _body.velocity.length()
    var blend_pos := clampf(speed / MAX_SPEED, 0.0, 1.0)
    _tree.set("parameters/locomotion/blend_position", blend_pos)
    GLog.debug("blend_position", "animation",
        {"value": blend_pos, "speed": speed}, null, GLog.SAMPLE_60)

func _apply_root_motion(_delta: float) -> void:
    var root_motion := _tree.get_root_motion_position()
    # Transform root motion from model space to world space
    var world_motion := _body.global_transform.basis * root_motion
    _body.velocity.x = world_motion.x / _delta if _delta > 0.0 else 0.0
    _body.velocity.z = world_motion.z / _delta if _delta > 0.0 else 0.0
    _body.move_and_slide()

func request_attack() -> void:
    _tree.set("parameters/attack_oneshot/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)
    GLog.info("attack_fired", "animation", {"blend_pos": _tree.get(
        "parameters/locomotion/blend_position")})
```

**Key points:**
- `blend_position` is driven by velocity magnitude, normalized to `[0, 1]`.
- Root motion is extracted per-frame and applied as velocity to `CharacterBody3D`.
- OneShot overlays attack on top of locomotion — locomotion continues blending underneath.
- `GLog.debug` with `SAMPLE_60` avoids flooding logs with per-frame blend values.

### GdUnit4 Test

```gdscript
class_name Character3DAnimationControllerTest
extends GdUnitTestSuite

const __source = "res://scripts/character3d/character3d_animation_controller.gd"

func test_blend_position_zero_when_idle() -> void:
    var runner := scene_runner("res://scenes/character3d/character3d.tscn")
    runner.set_time_factor(9.0)
    await runner.simulate_frames(10)
    var blend: float = runner.get_property("_tree").get(
        "parameters/locomotion/blend_position")
    assert_float(blend).is_equal_approx(0.0, 0.01)

func test_attack_oneshot_fires() -> void:
    var runner := scene_runner("res://scenes/character3d/character3d.tscn")
    runner.set_time_factor(9.0)
    runner.invoke("request_attack")
    await runner.simulate_frames(2)
    var request: int = runner.get_property("_tree").get(
        "parameters/attack_oneshot/request")
    # After firing, the request resets to ABORT or NONE
    assert_int(request).is_not_equal(
        AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)
```

---

## Procedural Tail (Verlet Chain)

A simple verlet integration chain pinned to a character, rendered with Line2D. No pre-authored animation — pure physics simulation.

### Scene Structure

```
Character (CharacterBody2D)
├── Sprite2D
├── TailAnchor (Marker2D)       ← pin point at character's back
└── VerletTail (Node2D)
    └── Line2D                   ← renders the chain
```

### Verlet Chain Script

```gdscript
class_name VerletTail
extends Node2D

@export var segment_count: int = 8
@export var segment_length: float = 6.0
@export var gravity: Vector2 = Vector2(0, 200)
@export var damping: float = 0.98
@export var iterations: int = 4

@onready var _anchor: Marker2D = %TailAnchor
@onready var _line: Line2D = $Line2D

var _points: PackedVector2Array
var _prev_points: PackedVector2Array

func _ready() -> void:
    var start := _anchor.global_position
    _points.resize(segment_count)
    _prev_points.resize(segment_count)
    for i in segment_count:
        _points[i] = start + Vector2(0, i * segment_length)
        _prev_points[i] = _points[i]

func _physics_process(delta: float) -> void:
    _simulate(delta)

func _process(_delta: float) -> void:
    _render()

func _simulate(delta: float) -> void:
    # Pin first point to anchor
    _points[0] = _anchor.global_position

    # Verlet integration
    for i in range(1, segment_count):
        var current := _points[i]
        var velocity := (current - _prev_points[i]) * damping
        _prev_points[i] = current
        _points[i] = current + velocity + gravity * (delta * delta)

    # Distance constraints
    for _iter in iterations:
        _points[0] = _anchor.global_position
        for i in range(1, segment_count):
            var dir := _points[i] - _points[i - 1]
            var dist := dir.length()
            if dist < 0.001:
                continue
            var error := (dist - segment_length) / dist * 0.5
            var correction := dir * error
            _points[i - 1] += correction
            _points[i] -= correction
        # Re-pin after constraint pass
        _points[0] = _anchor.global_position

func _render() -> void:
    # Convert global positions to local for Line2D
    var local_pts := PackedVector2Array()
    local_pts.resize(segment_count)
    for i in segment_count:
        local_pts[i] = to_local(_points[i])
    _line.points = local_pts
```

**Key points:**
- Simulation runs in `_physics_process` for deterministic results; rendering in `_process` for smooth visuals.
- Pin constraint is re-applied after each constraint iteration to prevent drift.
- `damping` < 1.0 prevents energy accumulation. Tune between 0.95–0.99.
- `iterations` controls stiffness — more iterations = stiffer chain.
- Line2D width curve can taper from thick (base) to thin (tip) for a natural tail look.

### GdUnit4 Test

```gdscript
class_name VerletTailTest
extends GdUnitTestSuite

const __source = "res://scripts/vfx/verlet_tail.gd"

func test_first_point_pinned_to_anchor() -> void:
    var runner := scene_runner("res://scenes/test/verlet_tail_test.tscn")
    runner.set_time_factor(9.0)
    await runner.simulate_frames(30)
    var points: PackedVector2Array = runner.get_property("_points")
    var anchor_pos: Vector2 = runner.get_property("_anchor").global_position
    assert_vector(points[0]).is_equal_approx(anchor_pos, Vector2(0.1, 0.1))

func test_chain_length_preserved() -> void:
    var runner := scene_runner("res://scenes/test/verlet_tail_test.tscn")
    runner.set_time_factor(9.0)
    await runner.simulate_frames(60)
    var points: PackedVector2Array = runner.get_property("_points")
    var seg_len: float = runner.get_property("segment_length")
    for i in range(1, points.size()):
        var dist := points[i].distance_to(points[i - 1])
        assert_float(dist).is_equal_approx(seg_len, 1.0)
```

---

## Validation Checklist

### Pre-Commit Checks

- [ ] AnimationController mediator exists for every entity using AnimationTree — no direct `play()` / `travel()` from gameplay code.
- [ ] `callback_mode_process = PHYSICS` set on AnimationMixer when animation drives physics state (hitboxes, movement).
- [ ] `callback_mode_method = IMMEDIATE` set when method tracks need frame-precise timing (combat hitboxes).
- [ ] `AnimatedSprite2D.play()` is guarded — never called every frame unconditionally.
- [ ] AnimationTree state machine has no orphan auto-advance loops (every transition has an explicit condition or is script-driven).
- [ ] Animation resources use `resource_local_to_scene = true` if mutated at runtime.
- [ ] Procedural simulation runs in `_physics_process`; rendering interpolates in `_process`.
- [ ] Typed GDScript throughout — no untyped variables or parameters.
- [ ] `get_diagnostics` returns no errors on changed `.gd` files.

### GLog Coverage Checklist

- [ ] `GLog.transition()` called for every animation state machine change (entity, from, to, trigger).
- [ ] `GLog.info()` called for animation-driven gameplay events: hitbox activation, combo start/end, cancel window usage.
- [ ] `GLog.debug()` with `SAMPLE_60` used for per-frame blend parameters — never unsampled.
- [ ] Category is `"animation"` for animation events; `"state"` is auto-set by `GLog.transition()`.
- [ ] `data` dictionaries are flat — no nested mutable objects.
- [ ] No GLog calls in hot loops without sampling.

### Performance Checklist

- [ ] BlendSpace2D has well-distributed blend points — no degenerate triangulation.
- [ ] Procedural animation constraint iterations ≤ 8 per chain (profile if higher).
- [ ] Verlet chains with > 50 segments flagged for potential `godot-gdextension-cpp` escalation.
- [ ] AnimationTree `advance_expression` not used for complex logic — keep it in the controller script.
- [ ] Root motion extraction uses `get_root_motion_position()` / `get_root_motion_rotation()` — not manual bone tracking.
- [ ] Animation method tracks do not allocate (no `Array.new()` or `Dictionary()` in called methods).
- [ ] SpriteFrames atlas textures are power-of-two and appropriately sized for target platform.
