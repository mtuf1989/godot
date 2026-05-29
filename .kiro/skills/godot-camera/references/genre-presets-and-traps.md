# Genre Presets and Hidden Traps

Copy-paste genre presets and the critical pitfalls that waste debugging time. Read this before finalizing any rig setup.

## Table of Contents

1. [Pipeline Order Cheat Sheet](#pipeline-order-cheat-sheet)
2. [Genre Presets](#genre-presets)
3. [Hidden Traps](#hidden-traps)

---

## Pipeline Order Cheat Sheet

```
2D: Focus → Composition → Constraints → Effects
    Follow/Group/Path → DeadZone/Lookahead/PlatformSnap → ZoomSnap/Limits → Shake

3D: Focus → Rotation → Constraints → Effects
    Follow or Orbit → LookAt (if using Follow) → Collision → Shake
```

---

## Genre Presets

### 2D Platformer

```
PlayerRig (VirtualCamera2D, priority = 10)
├── FollowModule2D          → SPRING, damping_x=6, damping_y=4
├── DeadZoneModule2D        → dead_zone: 0.25×0.1, soft_zone: 0.6×0.4
├── LookaheadModule2D       → VELOCITY, lookahead_time=0.35, horizontal_only=true
├── PlatformSnapModule2D    → snap_damping=8, fall_threshold=200
├── LimitsModule2D          → set to room boundaries
└── ShakeModule             → max_offset=(8,6), decay=1.5
```

### 2D Top-Down

```
PlayerRig (VirtualCamera2D, priority = 10)
├── FollowModule2D          → EXPONENTIAL, damping_x=5, damping_y=5
├── DeadZoneModule2D        → dead_zone: 0.15×0.15, soft_zone: 0.5×0.5
├── LookaheadModule2D       → INPUT, distance=80, horizontal_only=false
├── LimitsModule2D          → set to map boundaries
└── ShakeModule             → max_offset=(6,6), max_rotation=1
```

### 2D Fighting Game

```
ArenaRig (VirtualCamera2D, priority = 10)
├── GroupFrameModule2D      → 2 targets, margin=(100,60), min_zoom=0.4
├── ZoomSnapModule2D        → INTEGER, min_zoom=1, max_zoom=4 (pixel-art only)
├── LimitsModule2D          → arena boundaries
└── ShakeModule             → max_offset=(10,8), decay=2.0 (snappy)
```

### 2D Rail Camera

```
RailRig (VirtualCamera2D, priority = 10)
├── PathFollowModule2D      → target=Player, path=CameraRail, damping=4
├── LimitsModule2D          → optional
└── ShakeModule             → optional
```

### 3D Third-Person Orbit

```
PlayerRig (VirtualCamera3D, priority = 10)
├── OrbitFollowModule3D     → pivot_offset=(0,1.5,0), distance=5, mouse=true
├── CollisionModule3D       → pivot_offset=(0,1.5,0) ← MUST MATCH
└── ShakeModule             → max_offset=(8,6), max_rotation=2
```

### 3D Fixed Follow (Non-Interactive)

```
CinematicRig (VirtualCamera3D, priority = 5)
├── FollowModule3D          → offset=(0,3,6), damping=4
├── LookAtModule3D          → damping=8
└── ShakeModule             → optional
```

---

## Hidden Traps

Every item here comes from the actual implementation and test suite. The rig now validates many of these at startup and emits warnings.

### Trap 1: Forgetting `output_camera_path`

**Symptom:** Modules configured, camera doesn't move.
**Cause:** Director has nowhere to write computed state.
**Fix:** Set `output_camera_path` on the director. It warns at startup when empty.

### Trap 2: Camera2D Built-In Smoothing Fighting the Director

**Symptom:** Jitter, doubled smoothing, unexpected lag.
**Cause:** Output Camera2D has `position_smoothing_enabled`, `drag_horizontal_enabled`, `drag_vertical_enabled`, or `limit_smoothed` re-enabled after `_ready()`.
**Fix:** Never touch these on the output camera. The director disables them automatically.

### Trap 3: Two Focus Modules in One Rig

**Symptom:** One module's behavior is invisible.
**Cause:** Second focus module overwrites the first's `state.position`.
**Fix:** One focus module per rig. Use separate rigs with different priorities to switch between follow and group framing. The rig warns at startup.

### Trap 4: ShakeModule Before LimitsModule

**Symptom:** Shake feels weak near boundaries.
**Cause:** Limits clamp shake displacement.
**Fix:** ShakeModule always last child. The rig warns about out-of-order categories.

### Trap 5: Spring Flyback After Teleport

**Symptom:** Camera flies from old position to new after respawn/portal.
**Cause:** SPRING model maintains internal velocity.
**Fix:** Call `director.teleport()` AFTER moving the player. The module warns on >1000px jumps with near-zero velocity.

### Trap 6: Mismatched `pivot_offset` (3D)

**Symptom:** Camera clips through walls or retracts incorrectly.
**Cause:** OrbitFollowModule3D and CollisionModule3D have different `pivot_offset` values.
**Fix:** Keep `pivot_offset` identical on both. CollisionModule3D warns at startup on mismatch.

### Trap 7: Gamepad Orbit Actions Don't Exist

**Symptom:** Mouse orbit works, gamepad does nothing.
**Cause:** Default actions `"camera_yaw"` and `"camera_pitch"` don't exist in Input Map.
**Fix:** Create them in Project Settings → Input Map, or change action names on the module. Warns at startup.

### Trap 8: `process_priority` Doesn't Reorder Modules

**Symptom:** Changed `process_priority` on a module, nothing changes.
**Cause:** Pipeline uses tree order (child index), not `process_priority`.
**Fix:** Reorder by moving nodes in the scene tree (drag in editor or `move_child()` in code).

### Trap 9: DeadZone Debug Overlay Ships to Players

**Symptom:** Players see colored rectangles.
**Cause:** `debug_draw` left `true`.
**Fix:** Auto-disabled in release builds with a warning. Turn off in debug builds before shipping.

### Trap 10: PlatformSnap Ignores Non-CharacterBody2D

**Symptom:** PlatformSnapModule2D has no effect, no errors.
**Cause:** Target is not a CharacterBody2D (needs `is_on_floor()`).
**Fix:** Point at a CharacterBody2D. Warns once on wrong target type.

### Trap 11: Custom Blend Overrides With Invalid NodePaths

**Symptom:** Custom blend falls through to default_blend or CUT.
**Cause:** `from_rig` or `to_rig` NodePath doesn't resolve.
**Fix:** Use `director.get_path_to(rig)` for correct paths. Empty paths = wildcards.

### Trap 12: Rig Not Registering With Director

**Symptom:** Rig exists but director ignores it.
**Cause:** Director is not an ancestor of the rig and `director_path` is not set.
**Fix:** Make director an ancestor, or set `director_path` explicitly. Warns at startup.

### Trap 13: Blend-Out Rig Freed During Transition

**Symptom:** Blend completes abruptly.
**Cause:** Outgoing rig freed during blend. Director detects via `is_instance_valid()` and snaps.
**Fix:** Safe behavior (no crash), but keep rig alive until `blend_completed` if you want full duration.

### Trap 14: Non-Integer Zoom in Pixel-Art Games

**Symptom:** Sub-pixel artifacts, shimmering edges.
**Cause:** Non-integer zoom values.
**Fix:** Add ZoomSnapModule2D with `INTEGER` mode after any zoom-writing module.

### Trap 15: Lookahead Picks Up Idle Animation

**Symptom:** Camera drifts when player is still.
**Cause:** VELOCITY mode reads position deltas from idle animation bobbing.
**Fix:** Increase `direction_change_threshold` (0.15–0.3) or switch to INPUT mode.

### Trap 16: GroupFrame Zoom Feels Twitchy

**Symptom:** Zoom oscillates or feels unstable.
**Cause:** `zoom_damping` too high (equal to or higher than `position_damping`).
**Fix:** Keep `zoom_damping` at 40-60% of `position_damping`. Defaults (3.0 vs 5.0) are good.

### Trap 17: PathFollow With <2 Curve Points

**Symptom:** Module has no effect, no error.
**Cause:** Path2D curve is null or has <2 points.
**Fix:** Ensure Path2D has valid curve with ≥2 points. Warns once.

### Trap 18: Orbit Pitch at Exact ±90°

**Symptom:** Camera snaps or visual artifacts at extreme pitch.
**Cause:** `looking_at()` produces degenerate transform when look direction is parallel to up vector.
**Fix:** Keep `pitch_min`/`pitch_max` within ±80°. Defaults are safe.

---

## Teleport Protocol

Always call after discontinuous player movement:

```gdscript
player.global_position = new_spawn_point
director.teleport()  # AFTER moving the player
```

What it does:
1. Completes all active blends immediately
2. Calls `reset_state()` on every module (clears spring velocities, path parameters, noise time)
3. Calls `reset_physics_interpolation()` on the output camera

Call AFTER moving the player, not before. If no rig is active, it's a graceful no-op.

---

## Accessibility Checklist

- [ ] `shake_intensity_scale` exposed in settings UI (slider 0.0–1.0)
- [ ] `disable_all_shake` exposed in settings UI (toggle)
- [ ] OrbitFollowModule3D `mouse_sensitivity`, `invert_x`, `invert_y` exposed in settings
- [ ] Shipped game defaults shake to off or prompts player on first boot
- [ ] Player preference stored in ConfigFile
