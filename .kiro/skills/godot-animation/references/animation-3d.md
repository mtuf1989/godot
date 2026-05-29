# 3D Skeletal Animation Reference

Skeleton3D fundamentals, bone attachments, retargeting, inverse kinematics, ragdoll, and procedural bone modification for Godot 4.x.

## Table of Contents

1. [Skeleton3D Fundamentals](#skeleton3d-fundamentals)
2. [Animation Retargeting](#animation-retargeting)
3. [Inverse Kinematics](#inverse-kinematics)
4. [Procedural 3D Animation](#procedural-3d-animation)
5. [Common Failure Modes](#common-failure-modes)

---

## Skeleton3D Fundamentals

### Bone Hierarchy and Transforms

`Skeleton3D` stores a bone hierarchy as integer parent indices. `get_bone_parent()` returns `-1` for root bones; a parent index is always less than its child index. `get_bone_children()` returns direct children, `get_parentless_bones()` returns all root-level bones.

| Term | Meaning |
|------|---------|
| Rest transform | The initial/bind pose of a bone — defines the skeleton's default shape |
| Bone pose | The current transform used for skinning — in Godot 4.x this **includes** rest (not additive on top of rest as in 3.x) |
| Local pose | Position/rotation/scale relative to the parent bone |
| Global pose | Transform relative to the `Skeleton3D` node — **not** world space |

Local vs global pose API:

| Method | Space | Use Case |
|--------|-------|----------|
| `get/set_bone_pose_position()` | Skeleton-local | Lightweight procedural offsets |
| `get/set_bone_pose_rotation()` | Parent-bone-local (Quaternion) | Lean, aim, look-at corrections |
| `get/set_bone_pose_scale()` | Parent-bone-local | Squash-and-stretch accents |
| `get/set_bone_global_pose()` | Skeleton-global | Chain-space placement (triggers dirty recalc — avoid in hot loops) |

Prefer local pose setters for per-frame procedural work. Global pose setters trigger dirty-pose recalculation and degrade performance when other bones have already changed.

### BoneAttachment3D

Binds gameplay objects (weapons, VFX anchors, hitboxes) to skeleton bones.

| Property | Default | Behavior |
|----------|---------|----------|
| `override_pose` | `false` | **Follower mode** — node follows the bone transform |
| `override_pose` | `true` | **Override mode** — node drives the bone instead |

Use follower mode for props. Override mode can conflict with `SkeletonModifier3D` processing — prefer pose setters or the modifier stack for procedural bone control.

### Bone Pose Override

Direct pose setters (`set_bone_pose_position/rotation/scale`) are best as **last-mile corrections**: aim offsets, recoil, breathing expansion, squash-stretch accents.

The pose you set during `_process` is not necessarily the final pose — deferred modifier processing can overwrite it. To read the final modified pose, use the `SkeletonModifier3D.modification_processed` signal.

Modifier code should write the full desired correction and let the skeleton apply `influence` — do **not** apply your own influence interpolation inside a custom modifier.

### Physical Bones and Ragdoll

Ragdoll in Godot 4.x uses `PhysicalBoneSimulator3D` (controller) + `PhysicalBone3D` nodes (per-bone rigid bodies).

| Action | Method |
|--------|--------|
| Start full ragdoll | `PhysicalBoneSimulator3D.physical_bones_start_simulation()` |
| Start partial ragdoll | `physical_bones_start_simulation([&"BoneName1", &"BoneName2"])` |
| Stop simulation | `physical_bones_stop_simulation()` |
| Blend physics ↔ animation | Set `PhysicalBoneSimulator3D.influence` (0.0 = animation, 1.0 = physics) |

The older `Skeleton3D.physical_bones_start_simulation()` compatibility methods are deprecated — use the simulator node.

When starting partial simulation, pass **bone names** (StringName), not PhysicalBone3D node names.

```gdscript
@onready var anim_tree: AnimationTree = $AnimationTree
@onready var ragdoll: PhysicalBoneSimulator3D = $Skeleton3D/PhysicalBoneSimulator3D

func die() -> void:
    anim_tree.active = false
    ragdoll.physical_bones_start_simulation()

func hit_reaction_arm() -> void:
    ragdoll.influence = 0.5
    ragdoll.physical_bones_start_simulation([&"LeftUpperArm", &"LeftLowerArm"])

func recover() -> void:
    ragdoll.physical_bones_stop_simulation()
    anim_tree.active = true
```

---

## Animation Retargeting

### Why Retargeting Exists

Animation tracks reference bones by node path. Different rigs have different bone names, parent-child structures, and rest transforms. Even identically named bones may have different rest orientations. Retargeting solves both the **naming problem** and the **rest-space problem**.

Additional complication: Blender glTF exports carry edit-bone orientation in bone rest rotation, while Maya-style exports may not — the same animation file can arrive with fundamentally different rest conventions.

### SkeletonProfileHumanoid

The built-in humanoid profile containing **56 bones** in 4 groups.

| Property | Value |
|----------|-------|
| Profile root bone | `Root` |
| Scale base bone | `Hips` |
| Reference pose | T-pose |
| Coordinate system | Right-handed, Y-up, character faces +Z |
| Axis convention | +Y runs parent→child, +X rotation bends like contracting muscle |

Works best for standard bipeds. Non-humanoid rigs require custom `SkeletonProfile` resources with manual bone mapping.

### BoneMap Setup Flow

1. **Import destination character** — import as scene, open Advanced Import Settings
2. **Select Skeleton3D** — in the import dialog's scene tree
3. **Create BoneMap** — assign `SkeletonProfileHumanoid` as the profile
4. **Auto-map** — works if the rig uses common English bone names (pattern-based matching)
5. **Fix warnings** — red/magenta indicators mean missing or duplicate parent mappings
6. **Import motion sources** — import shared animations as `AnimationLibrary` with the same profile
7. **Enable rest fixers** as needed:

| Rest Fixer Option | Purpose |
|-------------------|---------|
| `Apply Node Transform` | Fixes source files with unapplied node-level transforms (common Blender issue) |
| `Normalize Position Tracks` | Uses `scale_base_bone` height to normalize stride between differently sized characters |
| `Overwrite Axis` | Rewrites bone rests to match profile reference pose — **most important** option for sharing animations |
| `Fix Silhouette` | Pushes near-compatible rigs (e.g., A-pose) closer to the profile's expected silhouette |

### Runtime Retargeting: RetargetModifier3D

`RetargetModifier3D` is a `SkeletonModifier3D` that transfers pose from a parent skeleton to a child skeleton in model space at runtime — different from import-time retargeting which rewrites assets up front.

| Property | Caveat |
|----------|--------|
| `use_global_pose = false` | Per-component control over position, rotation, scale — safer default |
| `use_global_pose = true` | Bone lengths must match exactly or bones expand/shrink; unmapped bones cause visual problems |

Use import-time retargeting for broad asset sharing. Use `RetargetModifier3D` for live pose transfer, preview tools, puppeteering, or characters following another skeleton at runtime.

### Retargeting Limitations

- `Fix Silhouette` cannot rescue rigs too far from the reference profile
- Heel height differences make feet look bad after silhouette fixing
- `Overwrite Axis` produces bad results if the source rest pose is semantically important
- Large proportion differences are only partly handled by `Normalize Position Tracks`
- Non-humanoid rigs need custom `SkeletonProfile` — auto-map requires common English names
- Bone roll issues are not solved by `Fix Silhouette`

---

## Inverse Kinematics

### SkeletonIK3D

Rotates a `Skeleton3D` bone chain so the tip bone reaches a target position. Uses FABRIK internally. Inherits from `SkeletonModifier3D` — runs after animation playback.

**Deprecation note:** `SkeletonIK3D` is marked deprecated in current stable docs. The newer `SkeletonModifier3D` stack includes `IKModifier3D` and other specialized classes. Existing `SkeletonIK3D` workflows still function in 4.x but may not be the long-term API.

| Property | Type | Purpose |
|----------|------|---------|
| `root_bone` | StringName | Start of the IK chain |
| `tip_bone` | StringName | End of the IK chain (effector) |
| `target` | Transform3D | Raw target transform |
| `target_node` | NodePath | Overrides `target` if set |
| `override_tip_basis` | bool | Tip bone copies target rotation |
| `use_magnet` | bool | Enable pole target |
| `magnet` | Vector3 | Pole target position (elbow/knee direction) |
| `max_iterations` | int | Solver accuracy (default 10) |
| `min_distance` | float | Stopping threshold |
| `influence` | float | Blend strength (inherited from SkeletonModifier3D) |

| Method | Behavior |
|--------|----------|
| `start()` | Begin continuous IK from next frame |
| `start(true)` | Apply IK immediately for one frame only |
| `stop()` | Stop solver, clear global-pose overrides |

```gdscript
@onready var ik: SkeletonIK3D = $Skeleton3D/RightArmIK
@onready var target: Marker3D = $RightHandTarget

func _ready() -> void:
    ik.root_bone = &"RightUpperArm"
    ik.tip_bone = &"RightHand"
    ik.target_node = target.get_path()
    ik.max_iterations = 8
    ik.min_distance = 0.01
    ik.override_tip_basis = true
    ik.influence = 1.0
    ik.start()
```

### FABRIK Solver Behavior

The internal FABRIK loop continues while:
- Tip-target distance > `min_distance`
- Improvement between iterations > `0.005`
- Iterations remaining in `max_iterations`

With magnet enabled, the chain first solves toward the magnet midpoint, then solves the full chain toward the final target. Pole targets work best on elbows and knees.

**Limitations:**
- No multi-tip / multi-end-effector solving
- No joint angle constraints (TODO in engine source)
- IK solver orthonormalizes the basis and reapplies original scale — IK does not affect bone scale

### Look-At Constraints

**LookAtModifier3D** (late 4.x): dedicated modifier for head/eye tracking — preferred when available.

**Manual approach** for version-agnostic head tracking:

```gdscript
func apply_head_look(skeleton: Skeleton3D, head_idx: int, target_pos: Vector3,
        max_yaw: float, max_pitch: float, weight: float) -> void:
    var head_global := skeleton.global_transform * skeleton.get_bone_global_pose(head_idx)
    var to_target := head_global.affine_inverse() * target_pos
    var yaw := clampf(atan2(to_target.x, -to_target.z), -max_yaw, max_yaw)
    var pitch := clampf(atan2(to_target.y, -to_target.z), -max_pitch, max_pitch)
    var look_rot := Quaternion(Vector3.UP, yaw) * Quaternion(Vector3.RIGHT, pitch)
    var base_rot := skeleton.get_bone_pose_rotation(head_idx)
    skeleton.set_bone_pose_rotation(head_idx, base_rot.slerp(base_rot * look_rot, weight))
```

Clamp yaw/pitch and blend only neck/head bones — rotating the entire upper spine is unstable.

### Procedural Foot Placement

Standard pattern: raycast → ankle target → pelvis offset → IK solve → ankle orient.

```gdscript
func update_foot_ik(foot_ik: SkeletonIK3D, foot_target: Marker3D,
        ray_origin: Vector3, space_state: PhysicsDirectSpaceState3D,
        ankle_height: float) -> bool:
    var query := PhysicsRayQueryParameters3D.create(
        ray_origin, ray_origin + Vector3.DOWN * 2.0
    )
    var result := space_state.intersect_ray(query)
    if result.is_empty():
        foot_ik.influence = lerp(foot_ik.influence, 0.0, 0.15)
        return false
    foot_target.global_position = result.position + Vector3.UP * ankle_height
    # Orient foot to surface normal
    var up := result.normal
    var fwd := -foot_target.global_basis.z
    fwd = (fwd - up * fwd.dot(up)).normalized()
    foot_target.global_basis = Basis(fwd.cross(up), up, -fwd)
    foot_ik.influence = lerp(foot_ik.influence, 1.0, 0.15)
    return true
```

Additional requirements for robust results:
- Smooth target movement to avoid popping
- Add pelvis compensation (lower hips) when either leg nears full extension
- Invalidate IK when contact is too far, too steep, or on wrong side

### IK + AnimationTree Interaction

Processing order: `AnimationMixer` playback → `SkeletonModifier3D` stack (including IK).

This means `AnimationTree` produces the base pose, then IK overwrites or blends the affected chain. Use `AnimationTree` for locomotion/state blending, IK for last-second contact corrections (hand placement, feet on slopes).

`AnimationTree` blend nodes support **track filters** — use them to isolate upper-body or lower-body layers before the IK pass.

At full `influence`, `SkeletonIK3D` overwrites the chain completely. Reduce `influence` to blend IK with the animated pose.

---

## Procedural 3D Animation

### Combining AnimationTree with Procedural Bone Modification

The safe pattern: let authored animation produce the base pose, apply procedural edits as a post-pass via the modifier stack or late `_process` writes.

Modifiers run after `AnimationMixer` playback. The pose read during normal processing is not always the final value — deferred modifier work may still change it. For reading the true final pose, use `SkeletonModifier3D.modification_processed`.

### Squash-and-Stretch via Bone Scale

Mechanically simple — `set_bone_pose_scale()` accepts non-uniform scale. The danger is mathematical:

`Basis` stores rotation + scale + possible shear. Rotating axes independently with non-uniform scale creates shear and visible distortion.

**Safe usage:** apply squash-stretch only on dedicated deformation bones with constrained axes, not on long chains of already-rotating gameplay bones. Volume preservation: if Y stretches by factor `s`, scale X and Z by `1/s`.

### Velocity-Based Lean and Tilt

Compute velocity in character-local space, derive a small pitch/roll, smooth it, apply to a few upper spine bones.

```gdscript
@export var skeleton_path: NodePath
@export var spine_bone: StringName = &"Spine"
@export var hips_bone: StringName = &"Hips"
@export var max_speed: float = 8.0

var _skeleton: Skeleton3D
var _spine_idx: int
var _hips_idx: int
var _spine_base_rot: Quaternion
var _hips_base_scale: Vector3

func _ready() -> void:
    _skeleton = get_node(skeleton_path)
    _spine_idx = _skeleton.find_bone(spine_bone)
    _hips_idx = _skeleton.find_bone(hips_bone)
    _spine_base_rot = _skeleton.get_bone_pose_rotation(_spine_idx)
    _hips_base_scale = _skeleton.get_bone_pose_scale(_hips_idx)

func apply_motion_style(velocity_local: Vector3) -> void:
    var speed_ratio := clampf(velocity_local.length() / max_speed, 0.0, 1.0)
    # Forward lean
    var lean := deg_to_rad(10.0) * clampf(-velocity_local.z / max_speed, -1.0, 1.0)
    _skeleton.set_bone_pose_rotation(_spine_idx, _spine_base_rot * Quaternion(Vector3.RIGHT, lean))
    # Vertical stretch at speed
    var s := 1.0 + speed_ratio * 0.08
    _skeleton.set_bone_pose_scale(_hips_idx, Vector3(
        _hips_base_scale.x / s, _hips_base_scale.y * s, _hips_base_scale.z / s
    ))
```

Cache the base pose to avoid drift from frame-to-frame compounding. Use local pose setters, not global.

### Spring-Based Secondary Motion

For hair, capes, tails, antennae — deterministic spring simulation without rigid bodies.

Per secondary bone, maintain a damped angular state, solve as a critically/underdamped spring toward a target pose each frame, write with `set_bone_pose_rotation()`.

```gdscript
var _current_rot: Quaternion
var _angular_velocity: Vector3

func solve_spring_bone(skeleton: Skeleton3D, bone_idx: int,
        target_rot: Quaternion, stiffness: float, damping: float, delta: float) -> void:
    var diff := (_current_rot.inverse() * target_rot).get_euler()
    _angular_velocity += diff * stiffness * delta
    _angular_velocity *= exp(-damping * delta)
    _current_rot *= Quaternion.from_euler(_angular_velocity * delta)
    _current_rot = _current_rot.normalized()
    skeleton.set_bone_pose_rotation(bone_idx, _current_rot)
```

**SpringBoneSimulator3D** (late 4.x): official modifier for spring-bone behavior — prefer it when available. Lives in the modifier stack with proper ordering guarantees.

### Jiggle Bones

One or two-bone spring chasing delayed rotation. Same principle as secondary motion at smaller amplitude.

Protections:
- **Clamp** maximum angular displacement — prevents explosion after teleports or large impulses
- **Orthonormalization awareness** — avoid stacking non-uniform scale + free-form rotation on the same bone (Basis shear)
- Reset spring state on teleport or scene transition

---

## Common Failure Modes

### Bone Transform Order

`Transform3D` = translation (`origin`) + `Basis` (rotation + scale + possible shear). There is no clean separation of "rotate then scale then translate" as independent values. Aggressive non-uniform scaling on animated bones creates distortion. `look_at()` warns about non-uniform scale. Procedural squash-stretch is safest on dedicated deformation bones.

### Retargeting Mismatches

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Limbs point wrong direction | Mismatched rest orientation | Enable `Overwrite Axis` in import settings |
| Auto-map misses bones | Non-standard naming | Manual mapping in BoneMap editor |
| Red/magenta warnings | Missing or duplicate parent mapping | Fix bone assignments in BoneMap |
| Feet float or sink | Proportion differences | `Normalize Position Tracks` + manual ankle offset |
| A-pose looks wrong after fix | `Fix Silhouette` limits exceeded | Re-export source in T-pose |
| `RetargetModifier3D` stretches bones | `use_global_pose = true` with mismatched bone lengths | Use `use_global_pose = false` or match bone lengths |

### IK Fighting AnimationTree

Ordering/ownership conflict. `SkeletonModifier3D` runs after animation playback, and `SkeletonIK3D` at full influence overwrites the chain. Conflicts arise when:
- Other gameplay code writes to the same bones
- `BoneAttachment3D.override_pose` is enabled on IK-affected bones
- Pose is read too early and written against stale data

**Fix:** establish clear ownership per bone set: base animation → filtered overlays → IK/procedural → physics/final modifiers.

### Physical Bones Not Activating

| Check | Detail |
|-------|--------|
| `simulate()` called? | Must call `PhysicalBoneSimulator3D.physical_bones_start_simulation()` — not the deprecated `Skeleton3D` method |
| Correct bone names? | Pass **bone names** (StringName), not PhysicalBone3D node names |
| Influence set? | `PhysicalBoneSimulator3D.influence` must be > 0 |
| AnimationTree still active? | For full ragdoll, set `anim_tree.active = false` first |

### Blender / Maya Import Issues

| Issue | Solution |
|-------|----------|
| Unapplied transforms | Apply all transforms in Blender before export; enable `Apply Node Transform` in Godot import |
| Wrong rest pose | Export in T-pose, not a deformed animation frame |
| Incorrect shading with blend shapes | Enable "Export Deformation Bones Only" in Blender's glTF export |
| Format issues | Prefer glTF for complex animated content; `.blend` import routes through Blender's glTF exporter internally |
| Bone naming defeats auto-map | Use common English bone names or map manually |
| Old Blender version friction | Use current Blender releases |

### Performance Guidelines

| Strategy | Detail |
|----------|--------|
| Animation LOD (manual) | Disable IK on distant NPCs, remove secondary-motion modifiers at distance |
| Reduce ragdoll cost | Remove tiny/utility bones from physics rigs — every `PhysicalBone3D` has simulation cost |
| Mesh LOD | Use import-generated LOD meshes; note skinned meshes can show artifacts when decimated |
| Visibility Range / HLOD | Swap distant characters to cheaper proxies with fewer nodes |
| IK iteration budget | Cost scales with active chains × bones per chain × `max_iterations` |
| Modifier count | Each `SkeletonModifier3D` runs every frame — remove or disable unused modifiers |

Godot does not publish per-skeleton performance budgets. Profile in your actual scene. Two-bone foot IK on a handful of nearby characters is usually fine; full-body or many-NPC IK at all distances is not.
