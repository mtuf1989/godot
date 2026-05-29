# Camera Module Reference

Complete API reference for every module in the camera_system addon. Organized by pipeline category.

## Table of Contents

1. [Core Classes](#core-classes)
2. [2D Modules](#2d-modules)
3. [3D Modules](#3d-modules)
4. [Shared Modules](#shared-modules)
5. [Blend System](#blend-system)
6. [Director API](#director-api)
7. [Rig API](#rig-api)
8. [Extending the System](#extending-the-system)

---

## Core Classes

### CameraState2D (RefCounted)

Flows through the 2D module pipeline each frame.

| Property | Type | Default |
|----------|------|---------|
| `position` | Vector2 | `Vector2.ZERO` |
| `zoom` | Vector2 | `Vector2.ONE` |
| `rotation` | float | `0.0` |
| `offset` | Vector2 | `Vector2.ZERO` |

Methods: `duplicate() -> CameraState2D`, `lerp_to(other, weight) -> CameraState2D`

### CameraState3D (RefCounted)

Flows through the 3D module pipeline each frame.

| Property | Type | Default |
|----------|------|---------|
| `transform` | Transform3D | `Transform3D.IDENTITY` |
| `fov` | float | `75.0` |
| `near` | float | `0.05` |

Methods: `duplicate() -> CameraState3D`, `lerp_to(other, weight) -> CameraState3D` (position=lerp, rotation=quaternion slerp)

### CameraModule (Node) — Abstract Base

All modules extend this. Defines the pipeline contract.

| Constant | Value | Meaning |
|----------|-------|---------|
| `Category.UNCATEGORIZED` | 0 | Skipped during validation |
| `Category.FOCUS` | 1 | Sets position/transform from scratch |
| `Category.COMPOSITION` | 2 | Refines position from focus module |
| `Category.CONSTRAINT` | 3 | Clamps to bounds or avoids geometry |
| `Category.EFFECT` | 4 | Adds displacement/noise on top |

Override methods:
- `process_camera(state: CameraState2D, delta: float) -> void`
- `process_camera_3d(state: CameraState3D, delta: float) -> void`
- `reset_state() -> void` — called on rig activation and `director.teleport()`

---

## 2D Modules

### FollowModule2D — FOCUS

Moves camera toward a target with configurable damping.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Must be Node2D |
| `offset` | Vector2 | `(0, 0)` | Pixel offset before damping |
| `damping_model` | DampingModel | `EXPONENTIAL` | `NONE`, `EXPONENTIAL`, `SPRING` |
| `damping_x` | float | `5.0` | Horizontal convergence rate (1/s) |
| `damping_y` | float | `5.0` | Vertical convergence rate (1/s) |

Damping models:
- **NONE** — hard lock, zero latency. Pixel-perfect/retro games.
- **EXPONENTIAL** — smooth catch-up decelerating near target. Most games (default).
- **SPRING** — critically-damped, settles without oscillation. Platformers, action.

Per-axis damping: `damping_x=8, damping_y=3` → snappy horizontal, smooth vertical (great for platformers).

### DeadZoneModule2D — COMPOSITION

Screen-space regions controlling camera responsiveness.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Must be Node2D |
| `dead_zone_width` | float | `0.2` | Viewport fraction (0.0–1.0) |
| `dead_zone_height` | float | `0.2` | Viewport fraction |
| `soft_zone_width` | float | `0.6` | Viewport fraction |
| `soft_zone_height` | float | `0.6` | Viewport fraction |
| `screen_x` | float | `0.5` | Zone center X (0=left, 1=right) |
| `screen_y` | float | `0.5` | Zone center Y (0=top, 1=bottom) |
| `soft_zone_damping` | float | `5.0` | Convergence rate |
| `debug_draw` | bool | `false` | Renders colored zone rectangles |

Three zones: dead (green, no camera movement) → soft (yellow, damped follow) → viewport edge (hard clamp).

Tip: `screen_x=0.35` frames player on left third — good for side-scrollers.

### LookaheadModule2D — COMPOSITION

Shifts camera toward where the player is heading.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Any Node2D |
| `mode` | Mode | `VELOCITY` | `VELOCITY` or `INPUT` |
| `lookahead_time` | float | `0.3` | Seconds of velocity to project (VELOCITY) |
| `lookahead_distance` | float | `100.0` | Pixels to offset (INPUT) |
| `input_action_left/right/up/down` | StringName | `"ui_left"` etc. | INPUT mode actions |
| `horizontal_only` | bool | `true` | Zeroes vertical component |
| `damping` | float | `3.0` | Smoothing rate |
| `direction_change_threshold` | float | `0.1` | Hysteresis to prevent oscillation |

### GroupFrameModule2D — FOCUS

Positions and zooms to keep multiple targets visible. Replaces FollowModule2D.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `targets` | Array[NodePath] | `[]` | Node2D paths |
| `weights` | Array[float] | `[]` | Per-target centroid weights (missing=1.0) |
| `margin` | Vector2 | `(50, 50)` | Pixel padding around bounding box |
| `min_zoom` | float | `0.5` | Zoomed-out limit |
| `max_zoom` | float | `2.0` | Zoomed-in limit |
| `framing_size` | float | `0.8` | Target fraction of screen to fill |
| `position_damping` | float | `5.0` | Position smoothing |
| `zoom_damping` | float | `3.0` | Zoom smoothing (keep 40-60% of position_damping) |

Edge cases: 1 target → follow at max_zoom. 0 targets → no-op. All weights zero → equal weights.

### PathFollowModule2D — FOCUS

Constrains camera to a Path2D rail. Replaces FollowModule2D.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Node2D to track |
| `path` | NodePath | `""` | Path2D defining the rail |
| `offset` | Vector2 | `(0, 0)` | Offset from sampled path position |
| `damping` | float | `5.0` | Smoothing rate for 1D path parameter |

Smooths the 1D scalar parameter (offset along curve), not the 2D position — prevents corner-cutting.

### PlatformSnapModule2D — COMPOSITION

Locks vertical camera during jumps, snaps to ground on landing.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | **Must be CharacterBody2D** |
| `snap_damping` | float | `8.0` | Vertical smoothing rate |
| `fall_threshold` | float | `200.0` | Pixels before camera follows a fall |
| `use_target_ground_check` | bool | `true` | Uses `is_on_floor()` |

Three states: grounded (tracks Y) → airborne hold (holds last ground Y) → airborne follow (resumes tracking during long falls).

### LimitsModule2D — CONSTRAINT

Clamps camera position so viewport stays within world-space bounds.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `left` | float | `-10000000.0` | Left boundary |
| `right` | float | `10000000.0` | Right boundary |
| `top` | float | `-10000000.0` | Top boundary |
| `bottom` | float | `10000000.0` | Bottom boundary |
| `smoothed` | bool | `false` | Exponential decay instead of hard clamp |
| `smooth_damping` | float | `10.0` | Convergence rate when smoothed |

Smart viewport handling: when viewport > bounded area on an axis, camera centers within bounds.

### ZoomSnapModule2D — CONSTRAINT

Snaps zoom to discrete steps for pixel-perfect rendering.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `mode` | SnapMode | `INTEGER` | `INTEGER`, `POWER_OF_TWO`, `CUSTOM_STEP` |
| `custom_step` | float | `0.5` | Step size for CUSTOM_STEP (must be >0) |
| `min_zoom` | float | `0.5` | Minimum zoom after snapping |
| `max_zoom` | float | `4.0` | Maximum zoom after snapping |

Place after any module that writes zoom (GroupFrameModule2D) and before LimitsModule2D.

---

## 3D Modules

### FollowModule3D — FOCUS

Fixed offset from target in target's local space. Position only — pair with LookAtModule3D for rotation.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Must be Node3D |
| `offset` | Vector3 | `(0, 2, 5)` | Local-space offset (2 up, 5 behind) |
| `damping` | float | `5.0` | Position convergence rate |

Offset is in target's local space. `(0, 2, 5)` = 2 above + 5 along target's +Z. If character faces -Z (Godot convention), camera is behind.

### LookAtModule3D — COMPOSITION

Smoothly rotates camera to face target via quaternion slerp. Rotation only.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Must be Node3D |
| `damping` | float | `8.0` | Rotation convergence rate |

Safety: skips rotation if camera is at exact same position as target (distance < 0.001).

Do NOT use with OrbitFollowModule3D — orbit handles its own rotation.

### OrbitFollowModule3D — FOCUS

Yaw/pitch/distance orbit driven by mouse and/or gamepad. Controls both position and rotation.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Node3D to orbit |
| `pivot_offset` | Vector3 | `(0, 1.5, 0)` | Offset to orbit pivot (eye height) |
| `default_distance` | float | `4.0` | Starting distance |
| `min_distance` | float | `1.0` | Closest allowed |
| `max_distance` | float | `10.0` | Farthest allowed |
| `pitch_min` | float | `-80.0` | Looking up limit (degrees) |
| `pitch_max` | float | `80.0` | Looking down limit (degrees) |
| `yaw_speed` | float | `0.1` | Gamepad yaw speed |
| `pitch_speed` | float | `0.1` | Gamepad pitch speed |
| `input_yaw_action` | StringName | `"camera_yaw"` | Gamepad yaw action |
| `input_pitch_action` | StringName | `"camera_pitch"` | Gamepad pitch action |
| `use_mouse` | bool | `true` | Read mouse relative motion |
| `mouse_sensitivity` | float | `0.003` | Radians per pixel |
| `invert_x` | bool | `false` | Invert horizontal |
| `invert_y` | bool | `false` | Invert vertical |
| `distance_curve` | Curve | `null` | Non-linear distance mapping |

Mouse motion accumulated in `_unhandled_input` at display rate, consumed in `process_camera_3d` at physics rate.

### CollisionModule3D — CONSTRAINT

Prevents camera clipping by pulling toward target when obstructed. Uses ShapeCast3D with sphere.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Node3D collision pivot |
| `pivot_offset` | Vector3 | `(0, 1.5, 0)` | **Must match OrbitFollowModule3D.pivot_offset** |
| `collision_mask` | int | `1` | Physics layers to check |
| `sphere_radius` | float | `0.3` | Collision sphere radius |
| `collision_margin` | float | `0.1` | Extra margin |
| `min_distance` | float | `0.5` | Closest from pivot |
| `retract_speed` | float | `25.0` | Pull-in speed (fast) |
| `recover_speed` | float | `6.0` | Push-out speed (slow) |

Asymmetric speed: fast retraction avoids visible clipping, slow recovery avoids fighting player input.

---

## Shared Modules

### ShakeModule — EFFECT (2D and 3D)

Trauma-based Perlin noise screen shake. Auto-detects 2D vs 3D pipeline.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `max_offset` | Vector2 | `(10, 8)` | Max displacement per axis |
| `max_rotation` | float | `2.0` | Max rotation in degrees |
| `noise_frequency` | float | `15.0` | Higher = faster oscillation |
| `trauma_decay_rate` | float | `1.5` | Trauma lost per second |
| `trauma_power` | float | `2.0` | Non-linear scaling exponent |

API:
```gdscript
shake.add_trauma(0.3)   # Add (stacks, clamped to 1.0)
shake.set_trauma(1.0)   # Set directly
shake.trauma            # Read current value
```

2D: additive displacement to position (X/Y) and rotation.
3D: additive displacement to transform.origin (X/Y) and roll around local Z axis.

Trauma model (Eiserloh GDC 2016): intensity = trauma^trauma_power. Multiple hits stack. Decays at trauma_decay_rate/s.

Director integration: reads `shake_intensity_scale` and `disable_all_shake` from nearest director each frame. No director = full intensity.

---

## Blend System

### CameraBlend (Resource)

| Property | Type | Default |
|----------|------|---------|
| `duration` | float | `0.5` |
| `curve` | Curve | `null` (linear) |
| `type` | BlendType | `LERP` |

BlendType: `LERP` (linear + lerp_angle for rotation), `SLERP` (same in practice), `CUT` (instant). Duration `0.0` = CUT.

### CameraBlendOverride (Resource)

| Property | Type | Notes |
|----------|------|-------|
| `from_rig` | NodePath | Empty = wildcard (any outgoing rig) |
| `to_rig` | NodePath | Empty = wildcard (any incoming rig) |
| `blend` | CameraBlend | The blend to use |

Paths resolved relative to director. Array order matters — first match wins. Place specific overrides before wildcards.

### Blend Precedence

1. `rig.blend_in` (highest)
2. `director.custom_blends` (Array[CameraBlendOverride], first match)
3. `director.default_blend` (fallback)
4. Instant CUT (if nothing set)

### Blend Stack

Limited to 2 entries. During blend, both outgoing and incoming rigs have pipelines evaluated (live blending). Third switch snaps oldest blend to completion.

---

## Director API

```gdscript
# Setup
director.output_camera_path = director.get_path_to(camera)
director.default_blend = blend_resource

# Custom blend overrides
var override := CameraBlendOverride.new()
override.from_rig = director.get_path_to($PlayerRig)
override.to_rig = director.get_path_to($BossRig)
override.blend = special_blend
director.custom_blends.append(override)

# Query
director.get_active_rig()       # → Node or null
director.is_blending()          # → bool
director.get_blend_progress()   # → float 0.0–1.0

# Actions
director.teleport()             # Reset after discontinuous movement

# Accessibility
director.shake_intensity_scale = 0.5   # 0.0–1.0
director.disable_all_shake = true
director.disable_fov_effects = true    # Forward-compatible placeholder

# Debug
director.debug_overlay_enabled = true

# Signals
director.rig_activated.connect(func(rig): ...)
director.rig_deactivated.connect(func(rig): ...)
director.blend_started.connect(func(from, to, blend): ...)
director.blend_completed.connect(func(from, to): ...)
```

---

## Rig API

```gdscript
rig.priority = 10          # Higher wins
rig.enabled = true         # false = excluded from evaluation
rig.blend_in = my_blend    # Per-rig blend override
rig.director_path = path   # Explicit director (optional)

# Signals
rig.became_active.connect(func(): ...)
rig.became_inactive.connect(func(): ...)
```

Priority tie-breaking: most recently registered rig wins (last to enter scene tree).

---

## Extending the System

### Custom 2D Module

```gdscript
class_name MyCustomModule2D
extends CameraModule

const MODULE_CATEGORY: int = CameraModule.Category.COMPOSITION

@export var my_parameter: float = 1.0

func process_camera(state: CameraState2D, delta: float) -> void:
    state.position.y -= my_parameter * 10.0

func reset_state() -> void:
    pass
```

### Custom 3D Module

```gdscript
class_name MyCustomModule3D
extends CameraModule

const MODULE_CATEGORY: int = CameraModule.Category.COMPOSITION

func process_camera_3d(state: CameraState3D, delta: float) -> void:
    # Modify state.transform, state.fov, state.near
    pass

func reset_state() -> void:
    pass
```

Place custom modules as rig children in the correct pipeline position for their category.
