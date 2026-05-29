# Physics Interpolation and Timing

## Core Concept

Physics interpolation stores previous and current transforms, then blends between them at render time:

```
display = lerp(previous, current, fraction_through_tick)
```

This smooths motion when monitor refresh ≠ physics tick rate (e.g., 60 TPS on 144 Hz display).

**Cost**: Display is 1–2 ticks behind "now." Godot prefers this over extrapolation because prediction can place objects in impossible locations.

### 2D vs 3D Difference

- **3D**: Interpolates each Node3D independently in global space
- **2D**: Interpolates in local space, inherited through CanvasItem tree. A child with interpolation OFF can still appear smooth if its parent is interpolated and moving.

---

## Setup

Enable: `Project Settings > Physics > Common > Physics Interpolation`

### The Rules

1. **All authoritative movement in `_physics_process()`** — never `_process()`
2. **Indirect movers too**: AnimationPlayer, Tween, nav followers driving interpolated objects must use physics timing
   - `AnimationMixer`: `ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS`
   - `Tween`: `.set_process_mode(TWEEN_PROCESS_PHYSICS)`
3. **`_process()` is for observation/presentation only** — camera smoothing, UI updates, cosmetic effects
4. **`Control` nodes are NOT interpolated by default** — keep HUD in separate CanvasLayer

---

## Teleports and Spawns

After any discontinuous position change, call `reset_physics_interpolation()` to prevent a one-frame streak from old position to new.

```gdscript
# Teleport
node.global_transform = new_transform
node.reset_physics_interpolation()

# Spawn (3D — set transform before or after add_child, then reset)
parent.add_child(instance)
instance.global_transform = spawn_transform
instance.reset_physics_interpolation()
```

### Reset Rules

- Call AFTER setting the new transform
- Propagates recursively to children
- Reset the **smallest branch that is semantically discontinuous**
- For moving-start objects (bullets): set initial transform → reset → set first-tick transform

### 2D Spawn Convenience

In 2D, `NOTIFICATION_RESET_PHYSICS_INTERPOLATION` fires on `NOTIFICATION_ENTER_TREE`. If you set transform before `add_child()`, first-frame state is usually handled automatically.

### Deferred Reset for Controls

Anchor/layout changes on `Control` are deferred. Reset must also be deferred:

```gdscript
panel.anchor_left = new_value
panel.call_deferred("reset_physics_interpolation")
```

---

## Tick Rate Selection

| TPS | Step Size | Avg Input Delay | Use Case |
|-----|-----------|-----------------|----------|
| 30 | 33.3 ms | ~16.7 ms | Mobile, simple physics, battery-constrained |
| 60 | 16.7 ms | ~8.3 ms | Default, most games |
| 120 | 8.3 ms | ~4.2 ms | Competitive FPS, VR, racing |

### Tradeoffs

- Higher TPS = more responsive input, more stable physics, more CPU cost
- Lower TPS = less CPU, needs interpolation more, less responsive
- Interpolation adds slight visual delay regardless of TPS

### max_physics_steps_per_frame

Default: 8. Limits catch-up work per rendered frame.

Slow-motion threshold = `TPS / max_physics_steps_per_frame`:
- 30 TPS → ~3.75 FPS
- 60 TPS → ~7.5 FPS
- 120 TPS → ~15 FPS

Below this, game intentionally slows down to prevent spiral of death. If you raise TPS, consider raising this too.

---

## Camera Architecture

Cameras are the most common case where you should step beyond automatic interpolation.

### Recommended Pattern

```gdscript
extends Camera3D

@export_node_path("Node3D") var target_path: NodePath
@export var offset := Vector3(0.0, 1.8, -5.0)
@export var follow_sharpness := 14.0
@export var mouse_sensitivity := 0.0025

var _target: Node3D
var _yaw := 0.0
var _pitch := 0.0
var _smoothed_pos := Vector3.ZERO

func _ready() -> void:
    _target = get_node(target_path)
    top_level = true  # Decouple from parent transform
    physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF

func _unhandled_input(event: InputEvent) -> void:
    if event is InputEventMouseMotion:
        _yaw -= event.relative.x * mouse_sensitivity
        _pitch = clamp(_pitch - event.relative.y * mouse_sensitivity,
            deg_to_rad(-75.0), deg_to_rad(70.0))

func _process(delta: float) -> void:
    if _target == null:
        return
    # Read the DISPLAYED pose, not raw physics pose
    var target_tr := _target.get_global_transform_interpolated()
    var orbit := Basis(Vector3.UP, _yaw) * Basis(Vector3.RIGHT, _pitch)
    var desired := target_tr.origin + orbit * offset
    
    var t := 1.0 - exp(-follow_sharpness * delta)
    _smoothed_pos = _smoothed_pos.lerp(desired, t)
    global_position = _smoothed_pos
    look_at(target_tr.origin + Vector3.UP * 1.3, Vector3.UP)
```

### Camera Rules

- `top_level = true`: Prevents parent motion from feeding into camera smoothing
- `physics_interpolation_mode = OFF`: Camera handles its own smoothing
- Follow `get_global_transform_interpolated()`: Matches what player sees on screen
- Mouse look in `_unhandled_input()` or `_process()`: Reacts on next rendered frame, not next physics tick
- After teleport/cut: snap `_smoothed_pos` immediately, don't lerp from old position

### Camera2D Caveat

Built-in `Camera2D` smoothing can jitter when FPS ≠ physics FPS. Fix: set camera's process callback to Physics, or disable automatic smoothing and follow manually in `_process()`.

---

## Reading Transforms

| Method | Returns | Use When |
|--------|---------|----------|
| `get_global_transform()` | Current physics state | Gameplay logic, collision response |
| `get_global_transform_interpolated()` | Displayed (rendered) state | Camera follow, visual alignment |

`get_global_transform_interpolated()` is for rare observer-style code. Don't drive gameplay from it.

---

## Debugging

**Test at 10 TPS** to make interpolation mistakes obvious. Godot explicitly recommends this.

### Common Symptoms

| Symptom | Likely Cause |
|---------|-------------|
| Object streaks on spawn | Missing `reset_physics_interpolation()` |
| Jitter on moving object | Transform set in `_process()` instead of `_physics_process()` |
| Camera micro-jitter | Camera reading raw transform, not interpolated; or mixing timelines |
| AnimationPlayer-driven body jitters | Animation not set to physics process mode |
| Tween-driven body jitters | Tween not set to `TWEEN_PROCESS_PHYSICS` |
| Child appears smooth but shouldn't | 2D: parent interpolation inherited visually |

---

## Engine.physics_jitter_fix

Default `0.5` smooths frame jitters. Set to `0` when using:
- Custom physics interpolation
- Rollback netcode
- Client prediction

Otherwise it adds an extra smoothing layer that conflicts with your own.

---

## Pixel-Perfect 2D

If you ship locked 60 Hz display with matching 60 TPS and prefer exact tick-snapped movement:
- `physics_interpolation_mode = OFF` is legitimate
- Price: any display cadence drift makes stutter visible again
- Consider for: fighting games, rhythm games, retro-style pixel art

---

## Particles and Interpolation

- GPU Particles2D: NOT currently interpolated
- CPUParticles2D: Default interpolation OFF (often looks better updating every frame)
- Particles are visual systems — keep on frame-driven updates
- After teleport: restart emitter explicitly rather than assuming inherited smoothing

### MultiMesh

Has dedicated per-instance reset methods and bulk reset API for instantaneous placement during physics tick.
