# 2D Animation Reference

Sprite animation systems (AnimatedSprite2D, AnimationPlayer-driven Sprite2D), spritesheet patterns, directional animation, and 2D procedural animation (verlet integration, inverse kinematics, spring-damper systems, creature locomotion, rope/cloth/soft-body simulation).

## Table of Contents

1. [AnimatedSprite2D + SpriteFrames](#animatedsprite2d)
2. [AnimationPlayer-Driven Sprite2D](#animationplayer-sprite2d)
3. [Decision Guide: AnimatedSprite2D vs AnimationPlayer](#decision-guide)
4. [Spritesheet Patterns](#spritesheet-patterns)
5. [Directional Animation](#directional-animation)
6. [Common Sprite Animation Pitfalls](#sprite-pitfalls)
7. [Verlet Integration](#verlet-integration)
8. [2D Inverse Kinematics](#2d-ik)
9. [Spring-Damper Systems](#spring-damper)
10. [Creature Locomotion Patterns](#creature-locomotion)
11. [Rope, Chain, and Cloth Simulation](#rope-chain-cloth)
12. [Soft Bodies and Deformable Shapes](#soft-bodies)
13. [Rendering Procedural Animation](#rendering-procedural)
14. [Procedural Animation Architecture](#procedural-architecture)
15. [Performance](#performance)
16. [Common Procedural Failure Modes](#procedural-failure-modes)

---

## 1. AnimatedSprite2D + SpriteFrames <a name="animatedsprite2d"></a>

`SpriteFrames` is a `Resource` holding named animations, each with per-animation FPS, loop flag, and a list of `Texture2D` frames with relative durations. Frames can be standalone PNGs or `AtlasTexture` slices from a shared sheet.

### Playback API

| Method / Property | Behavior |
|---|---|
| `play(anim, custom_speed, from_end)` | Start or restart animation. Speed multiplied by `speed_scale`. |
| `pause()` | Freeze at current frame and progress. Resumes from there. |
| `stop()` | Reset to frame 0, reset custom speed to 1.0. |
| `speed_scale` | Global multiplier. Negative reverses playback. |
| `frame` / `frame_progress` | Current frame index and sub-frame progress (0.0–1.0). |
| `is_playing()` | True even when `speed_scale == 0` (node won't advance but is "playing"). |

Effective frame duration: `relative_duration / (animation_fps * abs(playing_speed))`.

### Signals

| Signal | When Emitted | Caveat |
|---|---|---|
| `animation_finished` | Non-looping animation reaches last frame | **Not** emitted for looping animations. |
| `animation_looped` | Looping animation wraps around | — |
| `frame_changed` | Frame index changes during playback | **Not** emitted if new animation starts on same frame index as current. |
| `animation_changed` | `animation` property changes | — |

### frame_changed for Gameplay

The engine emits `frame_changed` inside the internal process loop after the frame index increments and a redraw is queued. The handler sees the **new frame already active**, making it usable for "on frame N, enable hitbox" logic. However, `set_frame_and_progress()` only emits when the numeric index actually changes — if an animation starts on frame 0 and the node was already on frame 0, no signal fires. Seed first-frame logic manually:

```gdscript
func start_attack() -> void:
    hitbox_shape.disabled = true
    anim.play(&"attack")
    _apply_attack_frame(anim.frame)  # seed frame 0 manually

func _on_frame_changed() -> void:
    _apply_attack_frame(anim.frame)

func _apply_attack_frame(idx: int) -> void:
    if anim.animation != &"attack":
        return
    hitbox_shape.disabled = not (idx >= 2 and idx <= 3)
```

### Process Timing

AnimatedSprite2D advances in `NOTIFICATION_INTERNAL_PROCESS` (idle timing, not physics). For physics-deterministic hitbox toggles, prefer AnimationPlayer with `callback_mode_process = PHYSICS`.

---

## 2. AnimationPlayer-Driven Sprite2D <a name="animationplayer-sprite2d"></a>

Set `Sprite2D.hframes` and `vframes` to match the sheet grid, then animate `Sprite2D.frame` on an AnimationPlayer timeline. The same timeline can key `flip_h`, `modulate`, `region_rect`, position, scale, and fire method tracks.

### Advantages Over AnimatedSprite2D

- **Method tracks** — call gameplay functions (`open_hitbox()`, `spawn_vfx()`) at precise timeline points.
- **Bezier tracks** — eased values for non-linear property changes.
- **Multi-property coherence** — one timeline drives frame, flash, flip, hitbox, and sound.
- **Callback control** — `callback_mode_process` (PHYSICS/IDLE/MANUAL) and `callback_mode_method` (DEFERRED/IMMEDIATE).
- **Blending and queueing** — `queue()`, `animation_set_next()`, per-transition blend times.

### Setup

```gdscript
@onready var sprite: Sprite2D = $Sprite2D
@onready var ap: AnimationPlayer = $AnimationPlayer

func _ready() -> void:
    ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS
    ap.callback_mode_method = AnimationMixer.ANIMATION_CALLBACK_MODE_METHOD_IMMEDIATE

func start_attack() -> void:
    ap.play(&"attack_slash")
    ap.advance(0.0)  # apply timeline state immediately — avoids one-frame mismatch
```

### Timing Pitfall

`play()` is not applied instantly — the animation updates on the next processing step. If you also change `flip_h` or another property in the same code path, call `advance(0)` after `play()` to force immediate application.

---

## 3. Decision Guide <a name="decision-guide"></a>

| Criterion | AnimatedSprite2D | AnimationPlayer + Sprite2D |
|---|---|---|
| Primary use | Frame flipper — named loops and one-shots | Sequencer — multi-property timelines |
| Authoring speed | Fast: drag frames, set FPS | Slower: key frame values on timeline |
| Gameplay events | `frame_changed` (coarse windows) | Method tracks (precise, authored) |
| Physics timing | Idle process only | PHYSICS / IDLE / MANUAL |
| Blending | None (hard cuts) | Per-transition blend times, crossfades |
| Queueing | Manual via `animation_finished` | Built-in `queue()`, `animation_set_next()` |
| Cancel/interrupt | Immediate hard cut | Authored crossfade via `play(anim, blend)` |
| Multi-property | Frame only | Frame + flash + flip + hitbox + sound |

**Rule of thumb:** If the problem is "play the right frames at the right speed," use AnimatedSprite2D. If the problem is "make the animation timeline drive gameplay and multiple properties in sync," use AnimationPlayer.

**Mixing:** Safe when ownership is clean — AnimatedSprite2D owns frame playback, AnimationPlayer owns orthogonal properties (scale, modulate, hitbox toggles). Never let both systems own frame selection on the same node.

---

## 4. Spritesheet Patterns <a name="spritesheet-patterns"></a>

| Format | When to Use |
|---|---|
| Horizontal strip | Single clip, `hframes = N, vframes = 1`, animate `frame`. Simplest. |
| Grid atlas | Multiple clips share one sheet. Rows = animations, columns = frames. |
| Individual frames | External art pipelines exporting one PNG per frame. Drag into SpriteFrames. |
| AtlasTexture slices | Reusable sub-rectangle resources from a shared atlas. Best for render-call locality. |

### AtlasTexture for SpriteFrames

SpriteFrames accepts any `Texture2D`. `AtlasTexture` points to a sub-rectangle of a larger texture, reducing texture fragmentation and render calls. Enable `filter_clip` to prevent atlas bleeding:

```gdscript
func _make_atlas_frame(atlas: Texture2D, region: Rect2) -> AtlasTexture:
    var t := AtlasTexture.new()
    t.atlas = atlas
    t.region = region
    t.filter_clip = true
    return t
```

### Texture Filtering

Since Godot 4.0, filter/repeat modes are controlled on `CanvasItem`, with a project default:
- `TEXTURE_FILTER_NEAREST` — pixel-art crispness, no blending.
- `TEXTURE_FILTER_LINEAR` — blends nearest four pixels, smooth HD art.

### Mipmaps and Compression

- **Mipmaps** improve quality when textures display smaller than native size but cost ~33% more memory. Only useful in 2D if the camera zooms out significantly.
- **Lossless** — default and recommended for 2D, especially pixel art.
- **Lossy** — acceptable for large HD 2D assets with slight artifacts.
- **VRAM Compressed** — avoid for 2D; introduces noticeable artifacts on lower-resolution textures.

---

## 5. Directional Animation <a name="directional-animation"></a>

### 4-Direction Pattern

Store `up`, `down`, and one sideways animation. Use `flip_h` for left:

```gdscript
func pick_4dir(prefix: StringName, dir: Vector2) -> StringName:
    if dir == Vector2.ZERO:
        return &"%s_idle" % prefix
    if absf(dir.x) > absf(dir.y):
        $AnimatedSprite2D.flip_h = dir.x < 0.0
        return &"%s_side" % prefix
    return &"%s_down" % prefix if dir.y > 0.0 else &"%s_up" % prefix
```

### 8-Direction Pattern

Map movement angle to index 0–7:

```gdscript
func pick_8dir(prefix: StringName, dir: Vector2) -> StringName:
    if dir == Vector2.ZERO:
        return &"%s0" % prefix
    var idx := wrapi(int(snappedf(dir.angle(), PI / 4.0) / (PI / 4.0)), 0, 8)
    return &"%s%d" % [prefix, idx]
```

---

## 6. Common Sprite Animation Pitfalls <a name="sprite-pitfalls"></a>

### Calling play() Every Frame

Calling `play()` in `_process()` restarts non-looping animations every frame. Guard:

```gdscript
func set_anim_safe(anim_name: StringName) -> void:
    if anim.animation != anim_name or not anim.is_playing():
        anim.play(anim_name)
```

### SpriteFrames Sharing

Resources are shared by default across scene instances. Runtime mutations (adding frames, changing FPS) affect all users. Duplicate when per-instance mutation is needed:

```gdscript
anim.sprite_frames = anim.sprite_frames.duplicate(true) as SpriteFrames
```

### frame_changed First Frame

`frame_changed` does not fire if the new animation starts on the same frame index the node was already on. Always seed first-frame logic manually when calling `play()`.

### Mixing Systems

AnimationPlayer keying `AnimatedSprite2D.frame` or `.animation` directly causes conflicts. Safe rule: AnimatedSprite2D owns frame playback; AnimationPlayer owns everything else. Or replace AnimatedSprite2D entirely with Sprite2D + AnimationPlayer.

### Pixel-Art Snapping

Centered sprites between pixels appear deformed. Enable `snap_2d_vertices_to_pixel` and `snap_2d_transforms_to_pixel` in project settings. Use `TEXTURE_FILTER_NEAREST`. For HD 2D, these snap settings cause stair-stepped movement — use intentionally.

---

## 7. Verlet Integration <a name="verlet-integration"></a>

Verlet integration updates position using current and previous positions, implicitly encoding velocity: `new_pos = pos + (pos - last_pos) * damping + accel * dt²`. Highly stable for linked particle systems (ropes, tails, cloth).

### GDScript Implementation

```gdscript
extends Node2D

var points: PackedVector2Array
var last_points: PackedVector2Array
var rest_lengths: PackedFloat32Array
var gravity := Vector2(0.0, 300.0)
var damping := 0.99
var iterations := 5
var pinned := PackedInt32Array()  # indices of fixed points

func _ready() -> void:
    var count := 10
    points.resize(count)
    last_points.resize(count)
    rest_lengths.resize(count - 1)
    for i in count:
        points[i] = Vector2(i * 10.0, 0.0)
        last_points[i] = points[i]
    for i in count - 1:
        rest_lengths[i] = 10.0
    pinned.append(0)

func _physics_process(delta: float) -> void:
    _integrate(delta)
    for _k in iterations:
        _solve_distance_constraints()
    _apply_pins()
    queue_redraw()

func _integrate(dt: float) -> void:
    for i in points.size():
        if i in pinned:
            continue
        var vel := (points[i] - last_points[i]) * damping
        last_points[i] = points[i]
        points[i] += vel + gravity * (dt * dt)

func _solve_distance_constraints() -> void:
    for i in points.size() - 1:
        var delta := points[i + 1] - points[i]
        var dist := delta.length()
        if dist < 1e-6:
            continue
        var diff := (dist - rest_lengths[i]) / dist
        var correction := delta * 0.5 * diff
        if not i in pinned:
            points[i] += correction
        if not (i + 1) in pinned:
            points[i + 1] -= correction

func _apply_pins() -> void:
    points[0] = global_position  # pin to node position

func _draw() -> void:
    draw_polyline(points, Color.WHITE, 2.0)
```

### Constraint Types

| Type | Purpose | Implementation |
|---|---|---|
| Distance | Fixed length between point pairs | Project endpoints to rest length each iteration |
| Angle | Stiffness between triplets | Enforce min/max bend angle at middle point |
| Pin | Fix point to world position or another node | Override position after integration |

### Damping

Multiply implicit velocity by a factor < 1.0 (e.g., 0.99). Lower values = heavier, less bouncy. The formal Verlet with friction: `x' = (2 - f)*x - (1 - f)*x_prev + a*dt²` where `f` is friction (0–1).

---

## 8. 2D Inverse Kinematics <a name="2d-ik"></a>

2D IK is simpler than 3D — each joint bends about a single axis normal to the plane.

### CCD (Cyclic Coordinate Descent)

Iterates joints from end to root, rotating each to bring the effector closer to the target. Fast, simple, good with angular clamps.

```gdscript
## CCD solver for a 2D joint chain.
## joints: Array[Dictionary] with keys: pos (Vector2), angle (float),
##         min_angle (float), max_angle (float), length (float)
func solve_ccd_2d(
    joints: Array, target: Vector2, iterations: int, tolerance: float
) -> void:
    var last := joints.size() - 1
    for _iter in iterations:
        _forward_kinematics(joints)
        if joints[last].pos.distance_to(target) <= tolerance:
            return
        for i in range(last - 1, -1, -1):
            _forward_kinematics(joints)
            var joint_pos: Vector2 = joints[i].pos
            var effector_pos: Vector2 = joints[last].pos
            var v_eff := (effector_pos - joint_pos).normalized()
            var v_tar := (target - joint_pos).normalized()
            var delta := atan2(v_eff.cross(v_tar), v_eff.dot(v_tar))
            delta = clampf(delta, -0.3, 0.3)  # limit per-iteration correction
            joints[i].angle += delta
            joints[i].angle = clampf(joints[i].angle, joints[i].min_angle, joints[i].max_angle)

func _forward_kinematics(joints: Array) -> void:
    var world_angle := 0.0
    var pos := joints[0].pos
    for i in range(1, joints.size()):
        world_angle += joints[i - 1].angle
        pos += Vector2.RIGHT.rotated(world_angle) * joints[i - 1].length
        joints[i].pos = pos
```

### FABRIK (Forward And Backward Reaching IK)

Works in point/line space — moves joint positions while preserving segment lengths. Converges in fewer iterations than CCD, smoother results.

```gdscript
## FABRIK solver. points: PackedVector2Array, lengths: PackedFloat32Array.
func solve_fabrik(
    points: PackedVector2Array, lengths: PackedFloat32Array,
    target: Vector2, root_pos: Vector2, iterations: int, tolerance: float
) -> void:
    var last := points.size() - 1
    var total_reach := 0.0
    for l in lengths:
        total_reach += l

    # Unreachable — align toward target
    if root_pos.distance_to(target) > total_reach:
        var dir := (target - root_pos).normalized()
        points[0] = root_pos
        for i in range(1, points.size()):
            points[i] = points[i - 1] + dir * lengths[i - 1]
        return

    for _iter in iterations:
        if points[last].distance_to(target) <= tolerance:
            return
        # Forward pass — end to root
        points[last] = target
        for i in range(last - 1, -1, -1):
            var dir := (points[i] - points[i + 1]).normalized()
            points[i] = points[i + 1] + dir * lengths[i]
        # Backward pass — root to end
        points[0] = root_pos
        for i in range(1, points.size()):
            var dir := (points[i] - points[i - 1]).normalized()
            points[i] = points[i - 1] + dir * lengths[i - 1]
```

### Godot Built-In 2D IK

Godot 4.x provides `Skeleton2D` + `Bone2D` with `SkeletonModification2DFABRIK` and `SkeletonModification2DCCDIK` via `SkeletonModificationStack2D`. Also available: `SkeletonModification2DJiggle` for spring-like secondary motion.

### Procedural Foot Placement

1. Compute ideal foothold ahead of character from gait phase and velocity.
2. Raycast downward to find valid ground contact point and normal.
3. Score candidates by reachability, slope, distance from ideal.
4. Lock foot to chosen contact during stance. Release when body moves far enough.
5. Adapt pelvis/root to planted feet — root follows feet, not the other way around.

---

## 9. Spring-Damper Systems <a name="spring-damper"></a>

A spring-damper exerts force: `F = -k * (x - target) - c * velocity`, where `k` = stiffness, `c` = damping.

| Regime | Condition | Behavior |
|---|---|---|
| Under-damped | `c < 2*sqrt(k)` | Oscillates, bouncy |
| Critically damped | `c = 2*sqrt(k)` | Fastest return, no overshoot |
| Over-damped | `c > 2*sqrt(k)` | Slow return, no oscillation |

### GDScript Spring Follower

```gdscript
extends Node2D

@export var target_path: NodePath
@export var stiffness := 100.0
@export var damping := 20.0  # ~2*sqrt(100) = 20 for critical damping

var _velocity := Vector2.ZERO
@onready var _target: Node2D = get_node(target_path)

func _physics_process(delta: float) -> void:
    var diff := _target.global_position - global_position
    var accel := diff * stiffness - _velocity * damping
    _velocity += accel * delta
    global_position += _velocity * delta
```

### Applications

- **Secondary motion** — hair, tails, cloth flaps, antenna. Attach visual elements to spring-driven points.
- **Spring-based following** — camera lag, trailing particles, weapon sway.
- **Impact response** — apply impulse to spring velocity on hit for reactive jiggle.

---

## 10. Creature Locomotion Patterns <a name="creature-locomotion"></a>

### Rain World Architecture

Rain World's procedural animation follows a layered pipeline with one-way dependency from simulation to cosmetics:

```
Intent → Target → Validation → Solve → Constraint → Cosmetic
```

1. **Intent** — controller decides velocity, climb state, gait phase, grip/release.
2. **Target** — each limb gets an ideal world-space target. Maintain ideal, temporary-best, and active-goal separately.
3. **Validation** — environment queries reject invalid positions (air, unreachable, bad slope). Score candidates; treat air as -∞ utility.
4. **Solve** — limbs use IK (two-bone, CCD, FABRIK). Body strips use verlet/PBD distance constraints.
5. **Constraint** — fixed lengths, angular limits, stretch caps, foot locks, collision projection.
6. **Cosmetic** — sprite deformation, line smoothing, segment thickness, secondary dangly bits. Depends on simulation but never feeds back into it.

Off-screen creatures run only intent + simulation (cheap). Cosmetic layer is skipped.

### Multi-Legged Gait

For spiders, insects, quadrupeds — alternating leg groups:

1. Track each foot's ideal anchor point (under hip/shoulder).
2. When `foot.distance_to(ideal_pos) > step_threshold`, initiate a step.
3. Lift foot, move target forward (raycast down from new ideal position), plant.
4. Other legs remain locked during the step.
5. Alternate groups: front-left + rear-right, then front-right + rear-left.

### Worm / Snake

Apply sinusoidal angle wave along a verlet chain: `angle[i] = A * sin(omega * t + phase * i)`. The verlet handles positions; the wave drives target angles per segment.

### Puppet-on-Strings

Few control points (head, tail) driven by input/AI. Rest of body follows via chains and springs. Body becomes a string puppet — simple to control, complex-looking output.

---

## 11. Rope, Chain, and Cloth Simulation <a name="rope-chain-cloth"></a>

### Rope

Single verlet chain with distance constraints. One end pinned. Render with Line2D — configure `width_curve` for taper, `gradient` for color, `texture_mode = TILE` for repeating rope texture.

### Chain

Like rope but with rigid links (high stiffness / many iterations). Render with rotated sprites per segment or MultiMesh with quads aligned between points.

### 2D Cloth

Grid (N×M) of verlet points with distance constraints between horizontal, vertical, and diagonal neighbors:

```gdscript
# Structural constraints: horizontal + vertical neighbors
# Shear constraints: diagonal neighbors
# Wind: add lateral force to points each frame
# Tearing: remove constraint when stretched beyond limit
```

Pin top-row points for hanging cloth. Render with `Polygon2D` — update `polygon` vertices from point positions each frame.

---

## 12. Soft Bodies and Deformable Shapes <a name="soft-bodies"></a>

Model as a network of verlet points connected by distance constraints (structural + shear + bending springs).

### Volume Preservation

After moving points, compute polygon area. If area exceeds rest area, apply inward pressure on all points; if less, push outward. This maintains consistent "volume" for blobs and slimes.

### Rendering

Assign point array to `Polygon2D.polygon` (`PackedVector2Array`) each frame. Fill with solid color, gradient, or UV-mapped texture. For smoother look, subdivide between simulation points.

---

## 13. Rendering Procedural Animation <a name="rendering-procedural"></a>

| Method | Best For | Notes |
|---|---|---|
| `Line2D` | Ropes, chains, simple trails | `width_curve` for taper, `gradient` for color, `texture_mode` for tiling |
| `Polygon2D` | Filled soft shapes, cloth panels | Update `polygon` vertices each frame |
| `_draw()` | Full control, debug, simple lines | `draw_polyline()`, `draw_colored_polygon()` |
| Sprite-based segments | Segmented creatures (caterpillar) | Position/rotate Sprite2D per segment along chain |
| Shader-assisted | GPU deformation of mesh | Upload point data as uniforms, deform vertices in shader |

### Z-Ordering

Overlapping segments need correct draw order. Use `z_index` per segment or `y_sort_enabled` on parent. Adjust for Line2D/Polygon2D overlap.

---

## 14. Procedural Animation Architecture <a name="procedural-architecture"></a>

### Simulation / Rendering Separation

- **Simulation** in `_physics_process(delta)` — fixed timestep, deterministic. Update verlet points, solve constraints, run IK.
- **Rendering** in `_process(delta)` — interpolate between physics states if needed, update Line2D/Polygon2D/sprites, call `queue_redraw()`.

### ProceduralBody Component

Encapsulate simulation in a dedicated node (e.g., `ProceduralSpine`, `VerletChain`). Expose key points (head, tail, attachment points) as properties. Controllers manipulate targets; the component generates motion.

### Integration with AnimationPlayer

Combine keyframe and procedural: AnimationPlayer switches states and animates base pose, procedural system adds continuous secondary motion (tail swing, breathing, impact response). Trigger procedural impulses alongside authored animations.

### Multiplayer

Procedural animation is typically local-only (visual). Sync high-level state (root position, velocity) and let each client run simulation independently. Small jiggles can differ per client.

---

## 15. Performance <a name="performance"></a>

### GDScript Ceiling

~100 verlet points with constraint solving at 60fps is a practical GDScript ceiling. Beyond that, frame time becomes noticeable.

### Optimization Strategies

| Strategy | When |
|---|---|
| `PackedVector2Array` / `PackedFloat32Array` | Always — avoid Array[Vector2] for inner loops |
| Fewer constraint iterations | When slight stretch is acceptable |
| Substeps | When stability matters more than speed |
| LOD | Reduce iterations or freeze simulation for distant creatures |
| Object pooling | Reuse chain nodes/arrays on spawn/despawn to avoid GC spikes |
| GDExtension C++ | When GDScript ceiling is hit — thousands of points, many creatures |

### When to Move to GDExtension

If profiling shows the verlet/spring inner loop consuming >2ms per frame, implement the solver in C++ via GDExtension. Keep the GDScript API (expose `points`, `apply_impulse()`, etc.) and move only the hot loop.

---

## 16. Common Procedural Failure Modes <a name="procedural-failure-modes"></a>

### Verlet Explosion

Points drift exponentially when constraints cannot be satisfied (too few iterations, impossible configuration). **Fix:** clamp maximum point displacement per frame, increase iterations, add substeps.

```gdscript
const MAX_DISPLACEMENT := 50.0

func _clamp_velocity() -> void:
    for i in points.size():
        var vel := points[i] - last_points[i]
        if vel.length() > MAX_DISPLACEMENT:
            last_points[i] = points[i] - vel.normalized() * MAX_DISPLACEMENT
```

### Spring Instability

High stiffness `k` with large `dt` causes overshoot. **Fix:** use critically damped parameters, run spring update in smaller substeps, or clamp acceleration.

### Rendering Artifacts

- Line2D with few points appears jagged — add interpolated points.
- Polygon2D with inverted winding from bad constraint solves flips normals — validate vertex order.
- Sprite segments with discontinuous rotation — smooth angle transitions between frames.

### Collision Tunneling

Fast-moving segments pass through thin colliders when only point positions are tested. **Fix:** use segment-based collision (raycast between consecutive points each step) or continuous collision with multiple raycasts.

### Joint Popping at Full Extension

IK chain snaps when target crosses exact reach boundary. **Fix:** test reachability before solving (FABRIK does this natively), add soft IK near extension limits, compensate with pelvis/body offset so the limb doesn't absorb 100% of error.

### Solver Fights and Jitter

Causes: terrain query noise (edges, thin colliders), parent/child solver ordering conflicts, cosmetic layer feeding back into simulation. **Fix:** lock feet during stance instead of re-evaluating every frame, clamp angular corrections, enforce one-way simulation→cosmetics dependency.
