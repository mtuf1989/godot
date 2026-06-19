# Camera Module Reference

Complete API reference for every module in the camera_system addon. Organized by pipeline category.

## Table of Contents

1. [Core Classes](#core-classes)
2. [2D Modules](#2d-modules)
3. [2D Scene Nodes](#2d-scene-nodes-non-module)
4. [3D Modules](#3d-modules)
5. [3D Scene Nodes](#3d-scene-nodes-non-module)
6. [Shared Modules](#shared-modules)
7. [Blend System](#blend-system)
8. [Director API](#director-api)
9. [Rig API](#rig-api)
10. [Extending the System](#extending-the-system)

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
| `ortho_size` | float | `0.0` |
| `look_at_bias` | Vector3 | `Vector3.ZERO` |
| `look_at_bias_weight` | float | `0.0` |

When `ortho_size > 0`, the director writes orthographic projection to the output camera. Default 0.0 = perspective.

Methods: `duplicate() -> CameraState3D`, `lerp_to(other, weight) -> CameraState3D` (position=lerp, rotation=quaternion slerp, ortho_size=lerp)

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

Director discovery (v3.1+):
- `get_director() -> Node` — lazy accessor with stale-reference recovery. Caches result, auto-re-finds if director is freed or module reparented.
- `_find_director() -> Node` — overridable tree-walk discovery. Subclasses can override for custom lookup.
- `_is_camera_director(node) -> bool` — static helper checking script class name chain.

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

### DeadZoneModule2D — FOCUS

Self-sufficient focus module with screen-space dead zone logic. **Replaces FollowModule2D** — do NOT use both in the same rig.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Must be Node2D |
| `offset` | Vector2 | `(0, 0)` | World-space framing offset |
| `zone_aspect` | ZoneAspect | `MANUAL` | `MANUAL`, `USE_WIDTH`, `USE_HEIGHT` |
| `dead_zone_width` | float | `0.2` | Viewport fraction (0.0–1.0) |
| `dead_zone_height` | float | `0.2` | Viewport fraction |
| `soft_zone_width` | float | `0.6` | Viewport fraction |
| `soft_zone_height` | float | `0.6` | Viewport fraction |
| `screen_x` | float | `0.5` | Zone center X (0=left, 1=right) |
| `screen_y` | float | `0.5` | Zone center Y (0=top, 1=bottom) |
| `soft_zone_damping` | float | `5.0` | Convergence rate (1/s, exponential decay) |
| `debug_draw` | bool | `false` | Renders colored zone rectangles |

Three zones: dead (green, no camera movement) → soft (yellow, damped follow) → viewport edge (hard clamp).

`zone_aspect` forces zones to square without manual tuning: `USE_WIDTH` makes height match width, `USE_HEIGHT` makes width match height.

Tip: `screen_x=0.35` frames player on left third — good for side-scrollers.

**Breaking change (v1.1):** This is a FOCUS module. It maintains its own internal camera position reference and takes full ownership of `state.position`. `soft_zone_damping` is a convergence rate (1/s) using exponential decay — higher values = faster correction.

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

### ConfinerModule2D — CONSTRAINT

Constrains camera viewport within a polygon boundary. Supports convex and concave polygons.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `boundary_shape` | NodePath | `""` | CollisionPolygon2D or Polygon2D |
| `damping` | float | `10.0` | Soft boundary damping (0 = hard clamp) |
| `slowing_distance` | float | `0.0` | Distance from edge where movement dampens |

Concave polygons are automatically decomposed into convex parts via `Geometry2D.decompose_polygon_in_convex()`. Each part is shrunk inward by viewport half-size (Minkowski difference). When viewport is larger than polygon on an axis, camera centers on that axis.

### HintReceiverModule — COMPOSITION

Biases camera position toward nearby CameraHint2D nodes using weighted average.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `reference_node` | NodePath | `""` | Distance reference (typically player) |

Discovers hints via `camera_hints_2d` group. Place after focus module, before constraints.

---

## 2D Scene Nodes (Non-Module)

### CameraZone2D (Area2D)

Spatial trigger that activates a virtual camera on body enter/exit.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target_camera` | NodePath | `""` | VirtualCamera2D to activate |
| `activation_mode` | ActivationMode | `PRIORITY_BOOST` | `PRIORITY_BOOST`, `ENABLE`, `CUSTOM` |
| `priority_boost` | int | `10` | Priority added in PRIORITY_BOOST mode |
| `blend_override` | CameraBlend | `null` | One-shot blend for this zone's transition |
| `tracking_group` | StringName | `"player"` | Only nodes in this group trigger |
| `min_hold_time` | float | `0.5` | Debounce seconds before deactivation |
| `director_path` | NodePath | `""` | Explicit director (optional) |

Signals: `zone_entered(body)`, `zone_exited(body)` (CUSTOM mode only).

### CameraHint2D (Node2D)

Point of interest that biases camera position when reference node is within influence radius.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `influence_radius` | float | `200.0` | World units |
| `weight` | float | `1.0` | Attraction strength (0–1) |
| `falloff` | Curve | `null` | Weight falloff (null = linear) |
| `affect_position` | bool | `true` | Whether to bias position |

Auto-discovered via `camera_hints_2d` group. Editor draws influence radius circle.

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

When `state.look_at_bias_weight > 0` (set by HintReceiverModule3D), blends look-at target toward `state.look_at_bias`. No behavior change when no hints are active.

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
| `auto_center_enabled` | bool | `false` | Drift yaw behind target facing |
| `auto_center_delay` | float | `2.0` | Idle seconds before centering |
| `auto_center_speed` | float | `2.0` | Yaw convergence rate |
| `auto_center_target` | NodePath | `""` | Node whose -Z = "behind" (falls back to target) |

Mouse motion accumulated in `_unhandled_input` at display rate, consumed in `process_camera_3d` at physics rate.

Auto-center: after `auto_center_delay` seconds of no input, yaw exponentially converges toward `auto_center_target`'s facing + PI. Any input resets the idle timer.

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

### FPSAnchorModule3D — FOCUS

Anchors camera to a player head/eye Node3D with zero latency. No smoothing — 1:1 with player controller rotation.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | Player's head/eye Node3D (not CharacterBody3D root) |
| `height_offset` | Vector3 | `(0, 0, 0)` | Additional local-space offset |

The player controller owns rotation. This module reads it. Pair with HeadBobModule3D, SwayModule3D, RecoilModule3D, FOVEffectModule3D.

### ShoulderModule3D — COMPOSITION

Over-the-shoulder lateral offset for TPS cameras. Supports shoulder swap and ADS blend.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `shoulder_offset` | Vector3 | `(0.6, 0, 0)` | Lateral offset (positive X = right) |
| `ads_offset` | Vector3 | `(0.4, 0, -1)` | Additional offset when aiming |
| `swap_speed` | float | `8.0` | Shoulder swap interpolation speed |
| `ads_speed` | float | `10.0` | ADS transition speed |

API:
```gdscript
shoulder.swap_shoulder()          # Toggle left/right
shoulder.set_ads(true)            # Blend toward ADS offset
shoulder.get_current_offset()     # Read effective offset
```

Signal: `shoulder_swapped(new_side: float)` — 1.0 = right, -1.0 = left.

### HeadBobModule3D — COMPOSITION

Velocity-driven Lissajous head bob. Vertical at 2× frequency, horizontal at 1×.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | **Must be CharacterBody3D** |
| `bob_frequency` | float | `2.0` | Base Hz, scales with speed |
| `vertical_amplitude` | float | `0.03` | Max Y displacement (meters) |
| `horizontal_amplitude` | float | `0.015` | Max X displacement (meters) |
| `speed_threshold` | float | `0.5` | Min speed to activate |
| `max_speed` | float | `6.0` | Speed for full amplitude |
| `sprint_frequency_multiplier` | float | `1.5` | Frequency boost when sprinting |
| `sprint_speed_threshold` | float | `8.0` | Speed above which sprint applies |

Respects `disable_head_bob` and `head_bob_intensity_scale` on director. **Default OFF** — opt-in only.

### SwayModule3D — COMPOSITION

Yaw-velocity roll, spring-driven landing dip, and optional strafe tilt.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `target` | NodePath | `""` | CharacterBody3D for landing/strafe |
| `roll_amount` | float | `0.3` | Roll per unit yaw velocity |
| `roll_damping` | float | `8.0` | Roll return speed |
| `landing_dip_amount` | float | `0.05` | Max landing displacement (meters) |
| `landing_spring_stiffness` | float | `150.0` | Landing recovery stiffness |
| `landing_spring_damping` | float | `12.0` | Landing recovery damping |
| `strafe_tilt_amount` | float | `0.0` | Strafe roll degrees (0 = disabled) |
| `strafe_tilt_damping` | float | `6.0` | Strafe tilt return speed |

Roll clamped to ±5°. Respects `disable_head_bob` and `head_bob_intensity_scale` (same comfort category).

### FixedAngleModule3D — COMPOSITION

Locks camera rotation to a fixed pitch/yaw or look-at orientation. For isometric/top-down/fixed-perspective.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `pitch` | float | `-55.0` | Degrees (negative = looking down) |
| `yaw` | float | `45.0` | Degrees (0 = north, 45 = classic iso) |
| `look_at_target` | bool | `true` | Orient toward target instead of raw angles |
| `target` | NodePath | `""` | Node3D to look at |

Place AFTER FollowModule3D. Follow sets position, this overrides rotation. Handles colinear up-vector for top-down (90° pitch).

### ZoomModule3D — COMPOSITION

Scroll-wheel zoom for 2.5D cameras. AUTO/ORTHOGRAPHIC/PERSPECTIVE modes.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `mode` | Mode | `AUTO` | `AUTO`, `ORTHOGRAPHIC`, `PERSPECTIVE` |
| `min_zoom` | float | `5.0` | Min ortho_size or distance |
| `max_zoom` | float | `20.0` | Max ortho_size or distance |
| `default_zoom` | float | `10.0` | Starting zoom |
| `zoom_speed` | float | `2.0` | Units per input press |
| `zoom_damping` | float | `8.0` | Smoothing rate |
| `input_zoom_in_action` | StringName | `"camera_zoom_in"` | Zoom in action |
| `input_zoom_out_action` | StringName | `"camera_zoom_out"` | Zoom out action |
| `target` | NodePath | `""` | Perspective dolly pivot (optional) |
| `ground_height` | float | `0.0` | Ground plane Y for pivot derivation |

AUTO detects from `state.ortho_size`: >0 = orthographic, 0 = perspective. Perspective mode repositions along view ray (dolly zoom). Without target, derives pivot from ground-plane intersection.

### PanModule3D — FOCUS

Free-floating tactics camera with keyboard, edge-scroll, and middle-mouse drag.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `pan_speed` | float | `15.0` | World units/second |
| `edge_scroll_enabled` | bool | `true` | Mouse-at-edge panning |
| `edge_scroll_margin` | float | `0.05` | Screen fraction from edge |
| `drag_enabled` | bool | `true` | Middle-mouse drag |
| `drag_sensitivity` | float | `0.02` | World units per pixel |
| `input_pan_left/right/forward/back` | StringName | `"camera_pan_*"` | Input actions |
| `bounds_min` | Vector2 | `(-100, -100)` | XZ min bounds |
| `bounds_max` | Vector2 | `(100, 100)` | XZ max bounds |
| `damping` | float | `10.0` | Movement smoothing |
| `camera_height` | float | `10.0` | Fixed Y position |

API:
```gdscript
pan.pan_to(Vector2(x, z))   # Smooth pan to world XZ
pan.snap_to(Vector2(x, z))  # Instant snap
```

Input gated behind rig-active check — no input leaks to inactive rigs.

### RotationStepModule3D — COMPOSITION

Discrete yaw rotation (90°/45° steps) with smooth interpolation. Rotates around ground pivot.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `step_degrees` | float | `90.0` | Degrees per step (90=4-dir, 45=8-dir) |
| `rotation_speed` | float | `8.0` | Interpolation speed |
| `input_rotate_cw` | StringName | `"camera_rotate_cw"` | Clockwise action |
| `input_rotate_ccw` | StringName | `"camera_rotate_ccw"` | Counter-clockwise action |

Must be placed AFTER FixedAngleModule3D (reads pitch from state that FixedAngle writes).

### UnitFocusModule3D — COMPOSITION

Snaps camera to a unit's XZ position on demand, auto-releases on arrival.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `focus_speed` | float | `8.0` | Pan speed toward unit |
| `arrival_threshold` | float | `0.1` | Distance for auto-release |

API:
```gdscript
unit_focus.focus_on(unit_node)  # Start panning to unit
unit_focus.cancel_focus()       # Cancel (e.g., player pans manually)
unit_focus.is_focusing()        # Query active state
```

### GroupFrameModule3D — FOCUS

Multi-target framing for brawler/co-op 3D cameras. Weighted centroid on XZ, AABB-based auto-zoom.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `targets` | Array[NodePath] | `[]` | Node3D paths |
| `weights` | Array[float] | `[]` | Per-target weights (missing=1.0) |
| `margin` | float | `3.0` | World-space padding |
| `min_size` | float | `8.0` | Min ortho_size or distance |
| `max_size` | float | `25.0` | Max ortho_size or distance |
| `framing_ratio` | float | `0.8` | Target viewport fill fraction |
| `position_damping` | float | `5.0` | Position smoothing |
| `zoom_damping` | float | `3.0` | Zoom smoothing |
| `height_offset` | float | `10.0` | Camera Y above centroid |

Pair with FixedAngleModule3D for rotation. Writes `state.ortho_size` when in orthographic mode.

### ConfinerModule3D — CONSTRAINT

Bounding volume (BoxShape3D) confinement with soft boundary damping.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `boundary_shape` | NodePath | `""` | BoxShape3D node |
| `damping` | float | `10.0` | Soft boundary damping |
| `slowing_distance` | float | `0.0` | Distance from edge for dampening |

Center-point clamping within the box volume.

### BoundsModule3D — CONSTRAINT

Hard-clamps camera XZ position to an axis-aligned rectangle (arena bounds).

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `bounds_min` | Vector2 | `(-50, -50)` | XZ minimum (x=minX, y=minZ) |
| `bounds_max` | Vector2 | `(50, 50)` | XZ maximum (x=maxX, y=maxZ) |

Update `bounds_min`/`bounds_max` from game code when entering new encounter areas.

### OrbitConstraintModule3D — CONSTRAINT

Restricts orbit yaw/pitch to a range or discrete allowed angles.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `mode` | Mode | `RANGE` | `RANGE` or `DISCRETE` |
| `yaw_min` | float | `-90.0` | RANGE mode min yaw (degrees) |
| `yaw_max` | float | `90.0` | RANGE mode max yaw (degrees) |
| `allowed_yaws` | Array[float] | `[0, 90, 180, 270]` | DISCRETE mode angles |
| `snap_speed` | float | `10.0` | DISCRETE snap interpolation |
| `pitch_min` | float | `-60.0` | Pitch clamp min |
| `pitch_max` | float | `-20.0` | Pitch clamp max |

Place AFTER OrbitFollowModule3D. DISCRETE mode for Captain Toad-style diorama cameras.

### HintReceiverModule3D — COMPOSITION

Biases camera position and writes `look_at_bias` toward nearby CameraHint3D nodes.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `reference_node` | NodePath | `""` | Distance reference (typically player) |

Discovers hints via `camera_hints_3d` group. Writes `state.look_at_bias` and `state.look_at_bias_weight` for downstream LookAtModule3D to consume.

---

## 3D Scene Nodes (Non-Module)

### CameraZone3D (Area3D)

3D equivalent of CameraZone2D. Same API and properties.

### CameraHint3D (Node3D)

3D point of interest with influence radius.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `influence_radius` | float | `10.0` | World units |
| `weight` | float | `1.0` | Attraction strength (0–1) |
| `falloff` | Curve | `null` | Weight falloff (null = linear) |
| `affect_position` | bool | `true` | Bias camera position |
| `affect_look_at` | bool | `false` | Bias look-at target |

Auto-discovered via `camera_hints_3d` group. Editor draws wireframe sphere gizmo.

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

### RecoilModule3D — EFFECT (3D only)

Spring-driven angular displacement for weapon recoil. Supports visual-only and mechanical recoil.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `spring_stiffness` | float | `150.0` | Higher = snappier return |
| `spring_damping` | float | `12.0` | Higher = less oscillation |
| `recovery_speed` | float | `5.0` | Mechanical recoil decay rate |
| `max_pitch_offset` | float | `15.0` | Max pitch displacement (degrees) |
| `max_yaw_offset` | float | `5.0` | Max yaw displacement (degrees) |

API:
```gdscript
recoil.apply_recoil(-3.0, 0.5, false)  # pitch_force, yaw_force, visual_only
recoil.apply_recoil(-1.0, 0.0, true)   # Visual-only (auto-recovers)
recoil.reset_recoil()                    # Clear all (weapon switch)
```

Signal: `mechanical_recoil_applied(pitch: float, yaw: float)` — connect to player controller to sync persistent aim shift. Values in radians.

Visual component respects `shake_intensity_scale`. Mechanical recoil is NOT affected by accessibility — it's gameplay-critical.

### FOVEffectModule3D — EFFECT (3D only)

Sprint FOV widening and ADS zoom narrowing with smooth exponential transitions.

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `base_fov` | float | `75.0` | Resting FOV |
| `sprint_fov_offset` | float | `10.0` | Added FOV when sprinting |
| `ads_fov` | float | `45.0` | FOV when aiming |
| `transition_speed` | float | `8.0` | Exponential decay rate |
| `ads_sensitivity_scaling` | float | `-1.0` | -1 = auto (focal length) |

API:
```gdscript
fov_effect.set_sprint(true)              # Enable sprint FOV
fov_effect.set_ads(true)                 # Enable ADS zoom
var scale = fov_effect.get_ads_sensitivity_scale()  # For mouse input scaling
```

Respects `disable_fov_effects` on director. Auto sensitivity scaling uses focal-length ratio: `tan(ads_half) / tan(base_half)`.

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

# One-shot blend override (highest precedence, consumed on next switch)
director.request_blend_override(special_blend)

# Query
director.get_active_rig()       # → Node or null
director.is_blending()          # → bool
director.get_blend_progress()   # → float 0.0–1.0

# Actions
director.teleport()             # Reset after discontinuous movement

# Cutscene context (stack-based, bypasses priority evaluation)
director.push_context(cutscene_rig, blend)  # Force rig active
director.pop_context(blend)                  # Restore previous
director.is_context_active()                 # → bool

# Accessibility
director.shake_intensity_scale = 0.5   # 0.0–1.0
director.disable_all_shake = true
director.disable_fov_effects = true

# 3D-only accessibility (CameraDirector3D)
director.head_bob_intensity_scale = 0.0  # Default OFF (opt-in)
director.disable_head_bob = true          # Default true (kills all bob/sway)

# Debug
director.debug_overlay_enabled = true

# Signals
director.rig_activated.connect(func(rig): ...)
director.rig_deactivated.connect(func(rig): ...)
director.blend_started.connect(func(from, to, blend): ...)
director.blend_completed.connect(func(from, to): ...)
```

### Blend Override Precedence (updated)

1. `director.request_blend_override()` (highest — one-shot, consumed on use)
2. `rig.blend_in`
3. `director.custom_blends` (Array[CameraBlendOverride], first match)
4. `director.default_blend` (fallback)
5. Instant CUT (if nothing set)

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
