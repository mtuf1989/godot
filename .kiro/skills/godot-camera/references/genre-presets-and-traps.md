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
    Follow/DeadZone/Group/Path → Lookahead/PlatformSnap/HintReceiver → Confiner/ZoomSnap/Limits → Shake

3D: Focus → Composition → Constraints → Effects
    Follow/Orbit/FPSAnchor/Pan/GroupFrame3D → LookAt/FixedAngle/Shoulder/HeadBob/Sway/Zoom/RotationStep/UnitFocus/HintReceiver3D → Confiner3D/Collision/Bounds/OrbitConstraint → Shake/Recoil/FOVEffect
```

---

## Genre Presets

### 2D Platformer

```
PlayerRig (VirtualCamera2D, priority = 10)
├── DeadZoneModule2D        → FOCUS, dead_zone: 0.25×0.1, soft_zone: 0.6×0.4, soft_zone_damping=6
├── LookaheadModule2D       → VELOCITY, lookahead_time=0.35, horizontal_only=true
├── PlatformSnapModule2D    → snap_damping=8, fall_threshold=200
├── LimitsModule2D          → set to room boundaries
└── ShakeModule             → max_offset=(8,6), decay=1.5
```

### 2D Top-Down

```
PlayerRig (VirtualCamera2D, priority = 10)
├── DeadZoneModule2D        → FOCUS, dead_zone: 0.15×0.15, soft_zone: 0.5×0.5, soft_zone_damping=5
├── LookaheadModule2D       → INPUT, distance=80, horizontal_only=false
├── HintReceiverModule      → reference_node=Player (optional, for POIs)
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

### 3D FPS (First-Person Shooter)

```
FPSRig (VirtualCamera3D, priority = 10)
├── FPSAnchorModule3D       → target=PlayerHead (eye Node3D)
├── HeadBobModule3D         → target=CharacterBody3D, bob_frequency=2.0
├── SwayModule3D            → target=CharacterBody3D, roll_amount=0.3
├── RecoilModule3D          → spring_stiffness=150, max_pitch_offset=15
└── FOVEffectModule3D       → base_fov=75, sprint_fov_offset=10, ads_fov=45
```

Notes:
- HeadBob/Sway are OFF by default (`disable_head_bob=true`). Player must opt in via settings.
- Connect `RecoilModule3D.mechanical_recoil_applied` to player controller for aim sync.
- Call `fov_effect.set_sprint(true/false)` and `fov_effect.set_ads(true/false)` from player controller.
- Read `fov_effect.get_ads_sensitivity_scale()` to scale mouse input during ADS.

### 3D TPS Over-the-Shoulder (Gears of War / RE4)

```
TPSRig (VirtualCamera3D, priority = 10)
├── OrbitFollowModule3D     → pivot_offset=(0,1.5,0), distance=3.5, auto_center_enabled=true
├── ShoulderModule3D        → shoulder_offset=(0.6,0,0), ads_offset=(0.4,0,-1)
├── CollisionModule3D       → pivot_offset=(0,1.5,0) ← MUST MATCH
├── ShakeModule             → max_offset=(6,5), decay=2.0
└── FOVEffectModule3D       → base_fov=70, sprint_fov_offset=8, ads_fov=50
```

Notes:
- Call `shoulder.swap_shoulder()` on input (e.g., D-pad left).
- Call `shoulder.set_ads(true)` when aiming.
- Auto-center drifts yaw behind player after 2s idle. Input interrupts immediately.

### 3D TPS Standard (No Shoulder)

```
TPSRig (VirtualCamera3D, priority = 10)
├── OrbitFollowModule3D     → pivot_offset=(0,1.5,0), distance=5, auto_center_enabled=false
├── CollisionModule3D       → pivot_offset=(0,1.5,0) ← MUST MATCH
└── ShakeModule             → max_offset=(8,6), max_rotation=2
```

### 2.5D Isometric ARPG (Diablo / Path of Exile)

```
IsoRig (VirtualCamera3D, priority = 10)
├── FollowModule3D          → target=Player, offset=(0,10,7), damping=5
├── FixedAngleModule3D      → pitch=-55, yaw=45, look_at_target=true
├── ZoomModule3D            → mode=ORTHOGRAPHIC, min_zoom=8, max_zoom=16, default_zoom=12
└── ShakeModule             → optional
```

Notes:
- Set initial `state.ortho_size` > 0 for orthographic projection.
- ZoomModule3D AUTO mode detects ortho from state.

### 2.5D Tactics (XCOM / Fire Emblem)

```
TacticsRig (VirtualCamera3D, priority = 10)
├── PanModule3D             → pan_speed=15, bounds_min/max=arena, camera_height=12
├── FixedAngleModule3D      → pitch=-60, yaw=0
├── RotationStepModule3D    → step_degrees=90, rotation_speed=8
├── ZoomModule3D            → mode=ORTHOGRAPHIC, min_zoom=6, max_zoom=20, default_zoom=10
├── UnitFocusModule3D       → focus_speed=8, arrival_threshold=0.1
└── BoundsModule3D          → bounds_min/max=map edges
```

Notes:
- Call `unit_focus.focus_on(unit)` when selecting a unit.
- Call `unit_focus.cancel_focus()` when player starts panning.
- Input actions required: camera_pan_*, camera_rotate_cw/ccw, camera_zoom_in/out.

### 2.5D Brawler / Arena (Smash Bros / Diorama)

```
ArenaRig (VirtualCamera3D, priority = 10)
├── GroupFrameModule3D      → targets=[P1,P2], margin=3, min_size=8, max_size=20
├── FixedAngleModule3D      → pitch=-45, yaw=0, look_at_target=false
├── ZoomModule3D            → mode=ORTHOGRAPHIC, min_zoom=8, max_zoom=25
├── BoundsModule3D          → bounds_min/max=arena edges
└── ShakeModule             → max_offset=(8,6), decay=2.0
```

### 2.5D Diorama (Captain Toad)

```
DioramaRig (VirtualCamera3D, priority = 10)
├── OrbitFollowModule3D     → target=Level, distance=15, pitch_min=-60, pitch_max=-20
├── OrbitConstraintModule3D → mode=DISCRETE, allowed_yaws=[0,90,180,270], pitch_min=-60, pitch_max=-20
└── ShakeModule             → optional
```

### 2D Room-Based (Metroidvania with Confiner)

```
PlayerRig (VirtualCamera2D, priority = 10)
├── FollowModule2D          → EXPONENTIAL, damping_x=5, damping_y=5
├── ConfinerModule2D        → boundary_shape=RoomPolygon, damping=10
└── ShakeModule             → optional
```

Notes:
- One ConfinerModule2D per rig. Change `boundary_shape` when entering new rooms.
- Concave room shapes are auto-decomposed. No manual convex splitting needed.

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

### Trap 19: DeadZoneModule2D Paired With FollowModule2D

**Symptom:** Dead zone has no visible effect, or pipeline validator warns about multiple FOCUS modules.
**Cause:** DeadZoneModule2D is a FOCUS module since v1.1. FollowModule2D overwrites position before dead zone runs.
**Fix:** Use DeadZoneModule2D INSTEAD of FollowModule2D. Remove FollowModule2D from the rig.

### Trap 20: HeadBob/Sway Enabled by Default

**Symptom:** Players report motion sickness on first play.
**Cause:** `disable_head_bob` left `false` or `head_bob_intensity_scale` set > 0 by default.
**Fix:** `disable_head_bob = true` and `head_bob_intensity_scale = 0.0` are the defaults. Expose as opt-in toggle in accessibility settings. Never ship with procedural locomotion motion enabled by default.

### Trap 21: RecoilModule Mechanical Recoil Without Signal Connection

**Symptom:** Camera kicks up on fire but aim doesn't actually shift — or aim desyncs from crosshair.
**Cause:** `mechanical_recoil_applied` signal not connected to player controller.
**Fix:** Connect the signal and apply the pitch/yaw delta to the player's persistent rotation:
```gdscript
recoil.mechanical_recoil_applied.connect(func(pitch, yaw):
    player.rotation.x += pitch
    player.rotation.y += yaw
)
```

### Trap 22: CameraZone tracking_group Mismatch

**Symptom:** Player enters zone area but camera doesn't switch.
**Cause:** Player node is not in the group specified by `tracking_group` (default: `"player"`).
**Fix:** Add the player to the `"player"` group, or change `tracking_group` to match.

### Trap 23: ConfinerModule2D Without Valid boundary_shape

**Symptom:** Confiner has no effect, no errors.
**Cause:** `boundary_shape` NodePath is empty or points at a node without polygon data.
**Fix:** Point at a CollisionPolygon2D or Polygon2D with ≥3 vertices.

### Trap 24: ZoomModule3D Perspective Without Target on Non-Flat Terrain

**Symptom:** Zoom pivot jumps erratically or camera clips through terrain.
**Cause:** Perspective mode derives pivot from ground-plane intersection at `ground_height`. Non-flat terrain breaks this assumption.
**Fix:** Set `target` NodePath to a Node3D at the look-at point. Only use `ground_height` fallback for flat terrain.

### Trap 25: RotationStepModule3D Before FixedAngleModule3D

**Symptom:** Rotation steps don't preserve the fixed viewing angle.
**Cause:** RotationStep reads pitch from state that FixedAngle writes. If FixedAngle runs after, it overwrites the rotation.
**Fix:** Pipeline order must be: FixedAngleModule3D → RotationStepModule3D.

### Trap 26: 2.5D Input Actions Not Created

**Symptom:** Pan/zoom/rotate modules do nothing, no errors.
**Cause:** Input actions (`camera_zoom_in`, `camera_pan_forward`, `camera_rotate_cw`, etc.) don't exist in Input Map.
**Fix:** Create them in Project Settings → Input Map. The modules check `InputMap.has_action()` and silently skip missing actions.

### Trap 27: push_context With Freed Rig

**Symptom:** Warning in output, cutscene doesn't activate.
**Cause:** Rig was freed before `push_context` was called.
**Fix:** Ensure the rig is alive. Use `is_instance_valid(rig)` before pushing. The director warns but no-ops gracefully.

### Trap 28: FOVEffectModule Without disable_fov_effects Exposed

**Symptom:** Players with motion sensitivity can't disable FOV punch.
**Cause:** `disable_fov_effects` not exposed in settings UI.
**Fix:** Bind `director.disable_fov_effects` to a settings toggle alongside shake settings.

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

## Cutscene Context Protocol

For scripted camera sequences that bypass normal priority evaluation:

```gdscript
# Start cutscene — force a specific rig
var blend := CameraBlend.new()
blend.duration = 1.0
director.push_context(cutscene_rig, blend)

# ... cutscene plays ...

# End cutscene — restore normal priority
director.pop_context(blend)
```

Contexts stack for nested cutscenes. `is_context_active()` queries whether a context is active. Pop without push is a graceful no-op.

---

## Accessibility Checklist

- [ ] `shake_intensity_scale` exposed in settings UI (slider 0.0–1.0)
- [ ] `disable_all_shake` exposed in settings UI (toggle)
- [ ] `disable_fov_effects` exposed in settings UI (toggle)
- [ ] `head_bob_intensity_scale` exposed in settings UI (slider 0.0–1.0, default OFF)
- [ ] `disable_head_bob` exposed in settings UI (toggle, default ON = disabled)
- [ ] OrbitFollowModule3D `mouse_sensitivity`, `invert_x`, `invert_y` exposed in settings
- [ ] Shipped game defaults head bob/sway to OFF (opt-in only)
- [ ] Shipped game defaults shake to off or prompts player on first boot
- [ ] Player preference stored in ConfigFile
