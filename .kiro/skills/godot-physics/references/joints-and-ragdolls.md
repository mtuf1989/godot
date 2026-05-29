# Joints, Constraints, and Ragdolls

## 2D Joints

### PinJoint2D (Revolute)

Removes 2 translational DOF, leaves 1 angular DOF. Use for: wheels, elbows, pendulums, chains.

- `softness`: How much forbidden motion is tolerated
- Angular limits available
- Motor: target velocity in rad/s

### GrooveJoint2D (Slot)

Constrains body B's anchor to a finite segment along the joint's local Y axis.

- `length`: Segment extent
- `initial_offset`: Anchor position along groove
- Use for: pistons, drawer slides, telescoping mechanisms
- Does NOT lock relative angle — add angular control separately if needed

### DampedSpringJoint2D

Spring-damper along the anchor line.

- `rest_length`: Equilibrium length
- `length`: Hard maximum stretch limit
- `stiffness`: Spring constant (k)
- `damping`: Damping ratio (0–1), NOT raw coefficient

```gdscript
# Grapple hook spring
func spawn_grapple(anchor: PhysicsBody2D, player: PhysicsBody2D, hit_point: Vector2) -> DampedSpringJoint2D:
    var joint := DampedSpringJoint2D.new()
    add_child(joint)
    joint.global_position = hit_point
    joint.node_a = joint.get_path_to(anchor)
    joint.node_b = joint.get_path_to(player)
    joint.disable_collision = true
    
    var rope_length := hit_point.distance_to(player.global_position)
    joint.length = rope_length
    joint.rest_length = rope_length * 0.65
    joint.stiffness = 40.0
    joint.damping = 0.9
    return joint
```

Reeling = reduce `rest_length` in `_physics_process()`.

### Joint2D Base Properties

- `node_a`, `node_b`: Connected bodies
- `bias`: Positional correction gain (0 = project default). Too high = chatter, too low = visible stretch.
- `disable_collision`: Disable body-body contact between connected pair

---

## 3D Joints

### PinJoint3D (Ball-and-Socket)

Constrains pivot points to stay coincident. All 3 angular DOF free.

- `params/bias`: Positional correction strength
- `params/damping`: Velocity matching
- `params/impulse_clamp`: Per-step impulse cap
- Use for: pendulums, rope links, generic attachments

### HingeJoint3D (1-DOF Rotation)

One rotation axis free, all else locked.

- Angular limits: `lower`, `upper`
- Motor: `target_velocity`, `max_impulse`
- `PARAM_LIMIT_SOFTNESS`: Deprecated, never used by engine
- Use for: doors, wheels, elbows, knees

```gdscript
func spawn_motor_hinge(
    parent: Node, frame: Node3D,
    body_a: PhysicsBody3D, body_b: PhysicsBody3D,
    lower_deg: float = -95.0, upper_deg: float = 5.0,
    target_vel: float = 1.5, max_impulse: float = 8.0
) -> HingeJoint3D:
    var joint := HingeJoint3D.new()
    parent.add_child(joint)
    joint.global_transform = frame.global_transform
    joint.node_a = joint.get_path_to(body_a)
    joint.node_b = joint.get_path_to(body_b)
    joint.exclude_nodes_from_collision = true
    joint.set("angular_limit/enable", true)
    joint.set("angular_limit/lower", deg_to_rad(lower_deg))
    joint.set("angular_limit/upper", deg_to_rad(upper_deg))
    joint.set("motor/enable", true)
    joint.set("motor/target_velocity", target_vel)
    joint.set("motor/max_impulse", max_impulse)
    return joint
```

Motor tuning: `target_velocity` = no-load speed, `max_impulse` = torque budget. High speed + small impulse = weak motor that stalls. Moderate speed + large impulse = strong servo.

### SliderJoint3D (Prismatic)

Translation along one axis. No built-in motor — use Generic6DOFJoint3D for powered slides.

### ConeTwistJoint3D (Ball-and-Socket with Limits)

- `swing_span`: Cone freedom
- `twist_span`: Axial rotation limit
- Twist axis = joint's local X axis
- Use for: shoulders, hips, hanging lamps

### Generic6DOFJoint3D (Universal)

Most powerful joint. First 3 DOF = linear, last 3 = angular. Each axis can be locked or limited.

- Per-axis motors and springs (linear and angular)
- Default: all limits enabled with zero span = fully locked
- Use for: suspension, powered pistons, custom mechanisms

```gdscript
# Suspension spring
func spawn_suspension(
    parent: Node, frame: Node3D,
    chassis: PhysicsBody3D, carrier: PhysicsBody3D,
    travel: float = 0.35, stiffness: float = 45.0, damping: float = 6.0
) -> Generic6DOFJoint3D:
    var joint := Generic6DOFJoint3D.new()
    parent.add_child(joint)
    joint.global_transform = frame.global_transform
    joint.node_a = joint.get_path_to(chassis)
    joint.node_b = joint.get_path_to(carrier)
    joint.exclude_nodes_from_collision = true
    
    # Lock X/Z translation
    for axis in ["x", "z"]:
        joint.set("linear_limit_%s/enabled" % axis, true)
        joint.set("linear_limit_%s/lower_distance" % axis, 0.0)
        joint.set("linear_limit_%s/upper_distance" % axis, 0.0)
    
    # Y travel with spring
    joint.set("linear_limit_y/enabled", true)
    joint.set("linear_limit_y/lower_distance", -travel)
    joint.set("linear_limit_y/upper_distance", 0.0)
    joint.set("linear_spring_y/enabled", true)
    joint.set("linear_spring_y/stiffness", stiffness)
    joint.set("linear_spring_y/damping", damping)
    
    # Lock all angular DOF
    for axis in ["x", "y", "z"]:
        joint.set("angular_limit_%s/enabled" % axis, true)
        joint.set("angular_limit_%s/lower_angle" % axis, 0.0)
        joint.set("angular_limit_%s/upper_angle" % axis, 0.0)
    return joint
```

### Joint3D Base Properties

- `node_a`, `node_b`: Connected bodies
- `exclude_nodes_from_collision`: Disable contact between linked pair
- `solver_priority`: Lower value = higher priority (solved first)

---

## Ragdoll System

### Setup Workflow

1. `PhysicalBoneSimulator3D` (SkeletonModifier3D) parents `PhysicalBone3D` nodes
2. Editor: "Create physical skeleton" on Skeleton3D auto-generates bones, shapes, joints
3. Delete tiny/utility bones — every simulated bone has runtime cost
4. Configure joint types per bone

### Joint Mapping for Human Anatomy

| Body Part | Joint Type | Why |
|-----------|-----------|-----|
| Shoulders, hips | ConeTwistJoint3D | Broad swing, bounded twist |
| Elbows, knees | HingeJoint3D | Single flexion axis |
| Wrists, ankles, neck | Narrow ConeTwist or Generic6DOF | Asymmetric limits |

### Activation

```gdscript
# Full ragdoll
simulator.physical_bones_start_simulation()

# Partial ragdoll (just an arm)
simulator.physical_bones_start_simulation(["UpperArm.L", "LowerArm.L", "Hand.L"])

# Stop
simulator.physical_bones_stop_simulation()
```

### Ragdoll-to-Animation Blend-Back

```gdscript
class_name RagdollBlendController
extends Node

@export var skeleton: Skeleton3D
@export var simulator: PhysicalBoneSimulator3D
@export var recovery_time: float = 0.35

var _captured_pose: Dictionary = {}
var _blend_left: float = 0.0

func capture_and_recover() -> void:
    _captured_pose.clear()
    for bone_idx in range(skeleton.get_bone_count()):
        _captured_pose[bone_idx] = skeleton.get_bone_global_pose(bone_idx)
    simulator.physical_bones_stop_simulation()
    _blend_left = recovery_time

func _process(delta: float) -> void:
    if _blend_left <= 0.0:
        return
    _blend_left = max(_blend_left - delta, 0.0)
    var weight := _blend_left / recovery_time
    for bone_idx in _captured_pose.keys():
        var ragdoll_pose: Transform3D = _captured_pose[bone_idx]
        var anim_pose: Transform3D = skeleton.get_bone_global_pose(bone_idx)
        var blended := ragdoll_pose.interpolate_with(anim_pose, 1.0 - weight)
        skeleton.set_bone_global_pose_override(int(bone_idx), blended, weight, true)
    if is_zero_approx(_blend_left):
        for bone_idx in _captured_pose.keys():
            skeleton.set_bone_global_pose_override(int(bone_idx), Transform3D(), 0.0, false)
        _captured_pose.clear()
```

**Timing**: Capture pose after `skeleton_updated` signal (post-modifier processing).

---

## Stability Checklist

When joints stretch, chains explode, or ragdolls jitter, work through in order:

1. **Disable collision on adjacent constrained parts** — `exclude_nodes_from_collision` (3D) / `disable_collision` (2D)
2. **Remove tiny utility bones from ragdolls** — every PhysicalBone3D costs performance
3. **Never scale physics bodies** — resize shape resources instead
4. **Thicken thin colliders, enable CCD for fast movers**
5. **Raise `physics_ticks_per_second`** before overdriving stiffness (120, 180, 240 Hz)
6. **Normalize masses along the chain** — large mismatches make convergence harder
7. **Give root-bearing joints highest priority** — `solver_priority` solved low-to-high
8. **Start from clean, non-overlapping rest poses**
9. **In 3D: try Jolt** — recommended by Godot's own troubleshooting docs for stability

### Breakable Joints

No built-in break threshold. Compute a proxy metric yourself:

```gdscript
# Monitor relative speed between connected bodies
var relative_speed := (body_a.linear_velocity - body_b.linear_velocity).length()
if relative_speed > break_threshold:
    joint.queue_free()
```

Under Jolt, `get_contact_impulse()` is an estimate — use relative speed as a more reliable proxy.
