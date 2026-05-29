# Animation Core Systems

Shared AnimationPlayer, AnimationMixer, and AnimationTree reference for both 2D and 3D animation in Godot 4.x.

## Table of Contents

1. [Track Types](#track-types)
2. [Animation Libraries](#animation-libraries)
3. [Playback Control](#playback-control)
4. [Callback Modes](#callback-modes)
5. [Signal Timing](#signal-timing)
6. [Common Pitfalls](#common-pitfalls)
7. [AnimationTree Architecture](#animationtree-architecture)
8. [State Machine Nodes](#state-machine-nodes)
9. [Blend Tree Nodes](#blend-tree-nodes)
10. [Filter Masks and Partial Body Blending](#filter-masks)
11. [Root Motion](#root-motion)
12. [Animation-Gameplay Integration Patterns](#integration-patterns)
13. [GLog Instrumentation](#glog-instrumentation)
14. [GdUnit4 Testing Patterns](#gdunit4-testing)

---

## 1. Track Types <a name="track-types"></a>

Each track targets a property path resolved relative to `AnimationMixer.root_node`.

| Track Type | Constant | When to Use |
|---|---|---|
| Value (Continuous) | `TYPE_VALUE` + `UPDATE_CONTINUOUS` | Numeric/vector/color properties that interpolate smoothly |
| Value (Discrete) | `TYPE_VALUE` + `UPDATE_DISCRETE` | Booleans, enums, sprite frame indices, visibility toggles — holds exact keyed value |
| Value (Capture) | `TYPE_VALUE` + `UPDATE_CAPTURE` | Only with `play_with_capture()` — blends from live scene pose into clip |
| Method | `TYPE_METHOD` | Gameplay callbacks: hitbox activation, VFX spawns, sound cues |
| Bezier | `TYPE_BEZIER` | Hand-shaped scalar curves, sub-property animation (e.g. `modulate:a`) |
| Audio | `TYPE_AUDIO` | Synchronized sound effects tied to animation timeline |
| Animation | `TYPE_ANIMATION` | Starts/stops another AnimationPlayer — does not blend into parent mixer |
| Blend Shape | `TYPE_BLEND_SHAPE` | 3D morph targets on MeshInstance3D — path must contain exactly one subname |

**Capture vs crossfade**: `play(custom_blend=...)` blends clip playback state. `play_with_capture()` snapshots live object values. Different mechanisms — marking a track as capture without calling `play_with_capture()` buys nothing.

**Method tracks**: gather all crossed keys per delta and call in order. On `seek()`, replay only one nearest key. `stop()`/`pause()` do not fire pending keys — mid-track stop is silent cancellation.

**callback_mode_discrete**: `DISCRETE_RECESSIVE` (AnimationPlayer default) vs `DISCRETE_FORCE_CONTINUOUS` (AnimationTree default — better blends, updates cache every frame).

---

## 2. Animation Libraries <a name="animation-libraries"></a>

AnimationMixer stores `AnimationLibrary` resources. Keys are `library_key/animation_key`. The empty-string library (the "global" library) omits the slash.

```gdscript
var global_lib := $AnimationPlayer.get_animation_library("")
global_lib.add_animation("flash", my_animation)
```

**Sharing**: same `AnimationLibrary` resource can be added to multiple players. Changes affect all users — each mixer connects to library change signals. Use `resource_local_to_scene` or `make_unique()` for per-instance modification.

**Runtime creation**: `Animation` API supports full track construction from code. Call `clear_caches()` after mutating an already-attached animation.

---

## 3. Playback Control <a name="playback-control"></a>

| Method | Effect |
|---|---|
| `play(name, custom_blend)` | Validates animation, builds crossfade, clears queue, emits `animation_started`. Same name resumes paused. |
| `play_with_capture(name, dur)` | Snapshots live object values, blends with Tween easing into clip |
| `pause()` | Stops processing, keeps position. Next `play()` resumes. |
| `stop()` | Resets to 0, clears blend stack/queue. Does **not** fire pending method/audio tracks. |
| `stop(true)` | `keep_state = true` — preserves current visual pose |
| `seek(time, true, true)` | `update_only` — pure visual repositioning, no side effects |
| `queue(name)` | FIFO. Loops block queued items forever. |
| `animation_set_next(from, to)` | When `from` finishes, `to` plays next automatically. Set once, persists. |

**Speed**: `speed_scale × custom_speed` = effective speed. `AnimationNodeTimeScale` in tree scales subtree independently — composes multiplicatively.

A fresh `play()` does not force immediate visual update — call `advance(0)` if needed. Manual `play()` clears the queue; queue-driven finish transitions preserve it.

---

## 4. Callback Modes <a name="callback-modes"></a>

| `callback_mode_process` | When to Use |
|---|---|
| `IDLE` | Cosmetic animation — smoothest at render framerate |
| `PHYSICS` | Animation drives physics state. **Mandatory** for combat. |
| `MANUAL` | Rollback/lockstep — call `advance(delta)` explicitly |

IDLE + physics gameplay = temporal mismatch → jitter, "looks right / collides wrong."

| `callback_mode_method` | Behavior |
|---|---|
| `DEFERRED` | Batches calls after processing. **Safer default.** |
| `IMMEDIATE` | Invokes on key reach. Can mutate playback mid-blend — use only with side-effect-disciplined methods. |

---

## 5. Signal Timing <a name="signal-timing"></a>

**`animation_finished`**: emitted after `_blend_apply()`, in `_blend_post_process()`. Suppressed if a method track changed the current animation. `seek()` to end does **not** emit — use `advance()` instead.

**`animation_changed(old, new)`**: only on queued animation takeover. Not emitted for `play()` or AnimationTree changes. Different from `AnimationLibrary.animation_changed` (internal cache signal).

| Loop Mode | Behavior |
|---|---|
| `LOOP_NONE` | Clamps, sets `end_reached` at edge |
| `LOOP_LINEAR` | Wraps via `fposmod()` |
| `LOOP_PINGPONG` | Reverses playback direction at boundaries — engine handles internally |

---

## 6. Common Pitfalls <a name="common-pitfalls"></a>

- **`play("missing")`** → loud `ERR_FAIL_COND_MSG`. **`queue("missing")`** → accepted silently, errors later.
- **Shared resources**: mutation affects all users. Use `resource_local_to_scene` or `make_unique()`.
- **Root node path**: tracks resolve relative to `root_node`. Reused animations in different hierarchies fail silently when node names differ.
- **Editor preview**: method tracks may not fire. Editor playback diverges from runtime intentionally.
- **`active = false`**: parks state without resetting. Re-enabling resumes from stored state, not from beginning.

---

## 7. AnimationTree Architecture <a name="animationtree-architecture"></a>

Inherits AnimationMixer. Shares libraries, callback modes, deterministic blending, root motion. Key difference: AnimationTree owns **graph-based blending**, AnimationPlayer owns **clip-based sequential playback**.

**Parameters**: dynamic `parameters/...` namespace from graph. Type-validated — write exact types (`bool` for conditions, `float` for blends, `Vector2` for blend positions). Every node auto-exposes read-only `current_length`, `current_position`, `current_delta`.

**`advance(delta)`**: clock step, not clip selection. Set parameters or call `travel()`/`start()` first, then step time.

```gdscript
func _ready() -> void:
    $AnimationTree.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
    $AnimationTree.active = true

func _physics_process(delta: float) -> void:
    $AnimationTree["parameters/Locomotion/blend_position"] = velocity.length()
    $AnimationTree.advance(delta)
```

**`active`**: disabling stops processing, preserves parameters. Re-enabling triggers restart/seek-style evaluation pass.

---

## 8. State Machine Nodes <a name="state-machine-nodes"></a>

### StateMachinePlayback

| Method | Behavior |
|---|---|
| `start(node, reset=true)` | Immediate start. `reset=true` plays from beginning. |
| `travel(to_node, reset_on_teleport=true)` | A* pathfinding through transitions. No path → **teleports**. |
| `next()` | Forces next queued hop. |
| `stop()` | Halts playback. |

`get_current_node()` changes when cross-fade **begins**, not ends.

### Transition Types

| Switch Mode | Behavior |
|---|---|
| `IMMEDIATE` | Blends into **beginning** of destination |
| `SYNC` | Seeks destination to **same position** as source |
| `AT_END` | Waits for current to finish, then switches |

Cross-fade: `xfade_time` + optional `xfade_curve`.

### Conditions and Advance Modes

- `advance_condition` → boolean at `parameters/conditions/<name>`, polled every tick.
- `advance_expression` → expression evaluated against `advance_expression_base_node`.
- `ADVANCE_MODE_DISABLED` / `ENABLED` (travel only) / `AUTO` (fires when condition true).
- Lower `priority` preferred. `break_loop_at_end` allows looping clips to break at cycle end.

### Nested Machines, End Node, Reset, Signals

Nested machines exit via `End` node. `reset` on transition: true = play from beginning, false = continue. `state_finished(state)` fires when fading-from influence is gone (more reliable than `get_current_node()`).

```gdscript
@onready var sm: AnimationNodeStateMachinePlayback = $AnimationTree["parameters/playback"]

func _physics_process(_delta: float) -> void:
    $AnimationTree["parameters/conditions/on_floor"] = is_on_floor()
    if Input.is_action_just_pressed("jump") and is_on_floor():
        sm.travel("jump")
    if Input.is_action_just_pressed("dash"):
        sm.start("dash", true)
```

---

## 9. Blend Tree Nodes <a name="blend-tree-nodes"></a>

| Node | Key Params | Notes |
|---|---|---|
| `BlendSpace1D` | `blend_position` (float) | Interpolated, discrete, or discrete-carry (carries playback position for frame-by-frame 2D) |
| `BlendSpace2D` | `blend_position` (Vector2) | Auto-triangulates. Clustered/collinear layouts → poor regions. |
| `OneShot` | `request` (FIRE/ABORT/FADE_OUT), `active` (read-only) | Request auto-clears. Check `active` for state. Supports additive mix. |
| `TimeScale` | `scale` (float) | Subtree-only. Negative reverses, 0 pauses. |
| `TimeSeek` | `seek_request` (float) | Auto-resets to -1.0. `explicit_elapse` processes intermediate keys. |
| `Add2` | `add_amount` (float) | Additive overlay. >1.0 amplifies, <0.0 inverts. |
| `Add3` | `add_amount` (float [-1,1]) | Separate `-add` and `+add` branches. |
| `Blend2` | `blend_amount` (float) | Linear interpolation between two poses. |
| `Blend3` | `blend_amount` (float [-1,1]) | Base + negative + positive. |
| `Transition` | `transition_request` (StringName) | Lightweight multi-way switch. Auto-clears request. |

**Add vs Blend**: Blend = choose between poses (interpolation). Add = layer deltas on base (addition, for recoil/aim/breathing).

---

## 10. Filter Masks and Partial Body Blending <a name="filter-masks"></a>

Filters are per-track-path, not semantic. `FILTER_PASS` (only filtered pass), `FILTER_STOP` (filtered blocked), `FILTER_BLEND` (filtered blended, rest pass through).

**Upper/lower split**: locomotion BlendSpace2D as base → Add2/OneShot overlay → filter to spine-up chain. Place filters immediately above the overlay branch.

**Mistakes**: wrong chain root (pelvis inclusion/exclusion), literal path matching (copied masks miss renamed bones, twist bones, IK targets).

---

## 11. Root Motion <a name="root-motion"></a>

`root_motion_track` on AnimationMixer — track path with bone. Mixer extracts transform, cancels it visually. `root_motion_local = true` for local-space position.

| Getter | Returns |
|---|---|
| `get_root_motion_position()` | Position delta (Vector3) |
| `get_root_motion_rotation()` | Rotation delta (Quaternion) |
| `get_root_motion_scale()` | Scale delta (Vector3) |

```gdscript
func _physics_process(delta: float) -> void:
    var world_delta: Vector3 = global_transform.basis * $AnimationTree.get_root_motion_position()
    velocity.x = world_delta.x / maxf(delta, 0.0001)
    velocity.z = world_delta.z / maxf(delta, 0.0001)
    velocity.y -= gravity * delta
    rotate_y($AnimationTree.get_root_motion_rotation().get_euler().y)
    move_and_slide()
```

**Root motion** = animation is authoritative locomotion (lunges, vaults). **In-place** = simpler for code/physics/rollback control. 2D: getters are 3D-valued — most 2D projects use in-place.

---

## 12. Animation-Gameplay Integration Patterns <a name="integration-patterns"></a>

### AnimationController Mediator

Gameplay code never calls `play()`/`travel()` directly. A dedicated script translates game state → animation parameters.

```gdscript
class_name AnimationController extends Node

@export var entity: CharacterBody3D
@export var tree: AnimationTree
@onready var sm: AnimationNodeStateMachinePlayback = tree["parameters/playback"]

func _physics_process(_delta: float) -> void:
    tree["parameters/Locomotion/blend_position"] = _get_planar_speed()
    match entity.current_state:
        entity.State.ATTACKING: sm.travel("attack_combo")
        entity.State.AIRBORNE:  sm.travel("air_loop")
        entity.State.GROUNDED:  sm.travel("locomotion")
```

### Method Tracks for Gameplay Events

Place keyframes at exact frames. Use `callback_mode_process = PHYSICS` for combat.

```gdscript
func _on_attack_active_start() -> void:
    weapon_hitbox.monitoring = true

func _on_attack_recovery_start() -> void:
    weapon_hitbox.monitoring = false
```

### Cancel Windows

Use method tracks to toggle `can_cancel` — not boolean property tracks (discrete tracks snap to RESET during blends).

```gdscript
var can_cancel: bool = false

func open_cancel_window() -> void:
    can_cancel = true

func close_cancel_window() -> void:
    can_cancel = false

func try_cancel_into(next_state: StringName) -> bool:
    if not can_cancel: return false
    can_cancel = false
    transition_to(next_state)
    return true
```

### Combo Queuing with Input Buffering

```gdscript
class_name InputBuffer extends Node

@export var buffer_window_ms: int = 350
var _buffer: Array[Dictionary] = []

func _unhandled_input(event: InputEvent) -> void:
    if event.is_action_pressed("attack"):
        _buffer.append({"action": "attack", "time": Time.get_ticks_msec()})

func consume(action: String) -> bool:
    var now := Time.get_ticks_msec()
    _buffer = _buffer.filter(func(e: Dictionary) -> bool:
        return now - int(e["time"]) <= buffer_window_ms)
    for i in _buffer.size():
        if _buffer[i]["action"] == action:
            _buffer.remove_at(i)
            return true
    return false
```

### Animation + Physics Coordination

| Scenario | Approach |
|---|---|
| Animation drives hitboxes | `PHYSICS` callback, method tracks toggle Area monitoring |
| Animation drives movement | Root motion in `_physics_process` → `move_and_slide()` |
| Physics drives animation | Controller reads velocity/floor state → blend parameters |
| Landing anticipation | Raycast detects floor early → crossfade before `is_on_floor()` |

Do **not** use `queue()` for combat — fails with loops, lacks dynamic blending.

---

## 13. GLog Instrumentation <a name="glog-instrumentation"></a>

### State Machine Transitions

```gdscript
func _transition_to(new_state: StringName) -> void:
    var old_state := sm.get_current_node()
    sm.travel(new_state)
    GLog.transition(entity, old_state, new_state, "animation_controller",
        {"blend_time": 0.15})
```

### Gameplay Events

```gdscript
func _on_attack_active_start() -> void:
    weapon_hitbox.monitoring = true
    GLog.info("hitbox_activated", "animation",
        {"attack": current_attack}, current_state)

func open_cancel_window() -> void:
    can_cancel = true
    GLog.info("cancel_window_open", "animation",
        {"attack": current_attack}, current_state)
```

### High-Frequency Blend Parameters

```gdscript
func _physics_process(_delta: float) -> void:
    GLog.debug("blend_position", "animation",
        {"value": tree["parameters/Locomotion/blend_position"]},
        null, GLog.SAMPLE_60)
```

| Category | Method | Rate |
|---|---|---|
| State change | `GLog.transition(entity, from, to, trigger, data)` | Every occurrence |
| Gameplay event | `GLog.info(event, cat, data, state?, sample?)` | Every occurrence |
| Blend param | `GLog.debug(event, cat, data, state?, sample)` | `GLog.SAMPLE_60` |

---

## 14. GdUnit4 Testing Patterns <a name="gdunit4-testing"></a>

### Animation State Test

```gdscript
func test_attack_plays_animation() -> void:
    var runner := scene_runner("res://scenes/player/player.tscn")
    runner.set_time_factor(9.0)
    var anim: AnimationPlayer = runner.scene().get_node("AnimationPlayer")
    await runner.simulate_key_pressed(KEY_SPACE)  # attack key
    await runner.simulate_frames(2)
    assert_str(anim.current_animation).is_equal("attack_1")
```

### Signal Testing

```gdscript
func test_animation_finished_emits() -> void:
    var runner := scene_runner("res://scenes/player/player.tscn")
    runner.set_time_factor(9.0)
    var anim: AnimationPlayer = runner.scene().get_node("AnimationPlayer")
    monitor_signals(anim)
    anim.play("attack_1")
    await runner.simulate_until_signal(anim, "animation_finished", [], 5.0)
    await assert_signal(anim).is_emitted("animation_finished")
```

### Cancel Window Test

```gdscript
func test_cancel_window_allows_dodge() -> void:
    var runner := scene_runner("res://scenes/player/player.tscn")
    runner.set_time_factor(9.0)
    runner.simulate_action_pressed("attack")
    await runner.simulate_frames(22)  # reach cancel window
    runner.simulate_action_pressed("dodge")
    await runner.simulate_frames(5)
    assert_str(runner.scene().get_node("AnimationPlayer").current_animation).is_equal("dodge_roll")
```

### Spy Verification

```gdscript
func test_controller_calls_travel() -> void:
    var spy_scene: Variant = spy("res://scenes/player/player.tscn")
    var runner := scene_runner(spy_scene)
    runner.set_time_factor(9.0)
    runner.invoke("set_state", "attacking")
    await runner.simulate_frames(2)
    verify(spy_scene, 1)._transition_to(any_string())
```

### Testing Tips

| Technique | Purpose |
|---|---|
| `set_time_factor(9.0)` | Speed up long animations in CI |
| `simulate_frames(N)` | Deterministic frame-level advancement |
| `simulate_until_signal(obj, sig, args, timeout)` | Wait for completion without hardcoded counts |
| `monitor_signals()` + `assert_signal().is_emitted()` | Verify signal emission |
| `spy()` + `verify(spy, count).method()` | Verify controller calls without full animation |
