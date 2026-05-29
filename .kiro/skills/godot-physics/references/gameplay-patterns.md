# Common Gameplay Physics Patterns

## 2D Platformer Controller

```gdscript
class_name CharacterMotor2D
extends CharacterBody2D

signal jumped
signal landed
signal dashed

@export var run_speed := 280.0
@export var ground_accel := 2200.0
@export var air_accel := 1200.0
@export var ground_friction := 2600.0

@export var jump_speed := 420.0
@export var fall_gravity_mult := 1.6
@export var jump_release_gravity_mult := 2.2
@export var coyote_time := 0.10
@export var jump_buffer := 0.12

@export var wall_slide_speed := 80.0
@export var wall_jump_x := 260.0
@export var wall_jump_y := 360.0

@export var dash_speed := 520.0
@export var dash_time := 0.12

var coyote_left := 0.0
var jump_buffer_left := 0.0
var dash_left := 0.0
var was_on_floor := false
var facing := 1

func _ready() -> void:
    motion_mode = CharacterBody2D.MOTION_MODE_GROUNDED
    floor_stop_on_slope = true
    floor_constant_speed = true
    floor_snap_length = 8.0

func _physics_process(delta: float) -> void:
    var gravity := get_gravity()
    var input_axis := Input.get_axis("move_left", "move_right")
    if input_axis != 0.0:
        facing = 1 if input_axis > 0.0 else -1

    # Buffer inputs
    if Input.is_action_just_pressed("jump"):
        jump_buffer_left = jump_buffer
    if Input.is_action_just_pressed("dash") and dash_left <= 0.0:
        dash_left = dash_time
        velocity = Vector2(facing * dash_speed, 0.0)
        dashed.emit()

    # Tick windows
    coyote_left = max(coyote_left - delta, 0.0)
    jump_buffer_left = max(jump_buffer_left - delta, 0.0)
    dash_left = max(dash_left - delta, 0.0)

    if is_on_floor():
        coyote_left = coyote_time

    if dash_left <= 0.0:
        # Horizontal movement
        var accel := ground_accel if is_on_floor() else air_accel
        var desired_x := input_axis * run_speed
        if input_axis != 0.0:
            velocity.x = move_toward(velocity.x, desired_x, accel * delta)
        elif is_on_floor():
            velocity.x = move_toward(velocity.x, 0.0, ground_friction * delta)

        # Jump (with coyote + buffer)
        if jump_buffer_left > 0.0 and (is_on_floor() or coyote_left > 0.0):
            jump_buffer_left = 0.0
            coyote_left = 0.0
            velocity.y = -jump_speed
            jumped.emit()
        # Wall jump
        elif is_on_wall_only() and Input.is_action_just_pressed("jump"):
            var wall_n := get_wall_normal()
            velocity = Vector2(wall_n.x * wall_jump_x, -wall_jump_y)
            jumped.emit()

        # Gravity with variable jump height
        if not is_on_floor():
            var grav_mult := 1.0
            if velocity.y > 0.0:
                grav_mult = fall_gravity_mult
            elif not Input.is_action_pressed("jump") and velocity.y < 0.0:
                grav_mult = jump_release_gravity_mult
            velocity += gravity * grav_mult * delta

        # Wall slide
        if is_on_wall_only() and velocity.y > wall_slide_speed:
            velocity.y = wall_slide_speed

    move_and_slide()

    if not was_on_floor and is_on_floor():
        landed.emit()
    was_on_floor = is_on_floor()
```

### Key Techniques

- **Variable jump**: Release jump early → multiply gravity (cuts arc short)
- **Coyote time**: Grace period after leaving edge (float countdown, not Timer)
- **Jump buffer**: Press jump slightly before landing (float countdown)
- **Wall slide**: Clamp fall speed when on wall + pressing toward it
- **Wall jump**: Use `get_wall_normal()` as kick-off direction

---

## 3D Character Controller

```gdscript
class_name CharacterMotor3D
extends CharacterBody3D

@export var run_speed := 6.5
@export var ground_accel := 28.0
@export var air_accel := 14.0
@export var ground_friction := 34.0
@export var jump_speed := 5.4
@export var fall_gravity_mult := 1.5

@export var yaw_basis_path: NodePath
@onready var yaw_basis_node := get_node_or_null(yaw_basis_path) as Node3D

var coyote_left := 0.0
var jump_buffer_left := 0.0

func _ready() -> void:
    motion_mode = CharacterBody3D.MOTION_MODE_GROUNDED
    floor_constant_speed = true
    floor_stop_on_slope = true
    floor_snap_length = 0.25
    platform_on_leave = CharacterBody3D.PLATFORM_ON_LEAVE_ADD_UPWARD_VELOCITY

func _physics_process(delta: float) -> void:
    var gravity := get_gravity()
    var input_2d := Input.get_vector("move_left", "move_right", "move_forward", "move_back")

    if Input.is_action_just_pressed("jump"):
        jump_buffer_left = 0.12
    coyote_left = max(coyote_left - delta, 0.0)
    jump_buffer_left = max(jump_buffer_left - delta, 0.0)
    if is_on_floor():
        coyote_left = 0.10

    # Camera-relative input
    var basis := yaw_basis_node.global_basis if yaw_basis_node else global_basis
    var desired_dir := (basis * Vector3(input_2d.x, 0.0, input_2d.y)).slide(up_direction).normalized()

    var up_vel := velocity.dot(up_direction)
    var planar_vel := velocity - up_direction * up_vel

    var accel := ground_accel if is_on_floor() else air_accel
    if desired_dir != Vector3.ZERO:
        planar_vel = planar_vel.move_toward(desired_dir * run_speed, accel * delta)
    elif is_on_floor():
        planar_vel = planar_vel.move_toward(Vector3.ZERO, ground_friction * delta)

    # Jump
    if jump_buffer_left > 0.0 and (is_on_floor() or coyote_left > 0.0):
        jump_buffer_left = 0.0
        coyote_left = 0.0
        up_vel = jump_speed
    elif not is_on_floor():
        var grav_mult := fall_gravity_mult if up_vel < 0.0 else 1.0
        up_vel += gravity.dot(up_direction) * grav_mult * delta

    velocity = planar_vel + up_direction * up_vel
    move_and_slide()
```

---

## Stair Stepping (3D)

No built-in solution. Two approaches:

### Approach 1: SeparationRayShape3D

Add as auxiliary shape on character. It "instantly moves up" when touching stairs. Good for simple cases.

### Approach 2: Script-Level Geometry Probe

```gdscript
class_name StairStepper3D
extends RefCounted

func try_step_up(
    body: CharacterBody3D,
    horizontal_motion: Vector3,
    max_step_height: float,
    world_mask: int
) -> bool:
    if horizontal_motion.is_zero_approx():
        return false

    var up := body.up_direction.normalized()
    var start := body.global_transform
    var raised := start.translated(up * max_step_height)

    # If raised pose still collides horizontally, it's a wall not a step
    if body.test_move(raised, horizontal_motion):
        return false

    # Raycast down from raised+forward position to find step surface
    var end_origin := raised.origin + horizontal_motion
    var query := PhysicsRayQueryParameters3D.create(
        end_origin, end_origin - up * (max_step_height + 0.1))
    query.exclude = [body.get_rid()]
    query.collision_mask = world_mask

    var hit := body.get_world_3d().direct_space_state.intersect_ray(query)
    if hit.is_empty():
        return false

    var floor_angle := acos(clamp(hit["normal"].dot(up), -1.0, 1.0))
    if floor_angle > body.floor_max_angle:
        return false

    body.global_position = hit["position"]
    body.apply_floor_snap()
    return true
```

Call before `move_and_slide()` when horizontal collision is detected.

---

## Projectile Patterns

### Hitscan (Instant)

```gdscript
func fire_hitscan(from: Vector3, to: Vector3, mask: int, exclude: Array[RID] = []) -> Dictionary:
    var query := PhysicsRayQueryParameters3D.create(from, to, mask, exclude)
    return get_world_3d().direct_space_state.intersect_ray(query)
```

### Kinematic Projectile (Swept)

```gdscript
extends CharacterBody2D

@export var speed := 1200.0
@export var lifetime := 3.0

var direction := Vector2.RIGHT

func _physics_process(delta: float) -> void:
    lifetime -= delta
    if lifetime <= 0.0:
        queue_free()
        return
    velocity = direction * speed
    var collision := move_and_collide(velocity * delta)
    if collision:
        # Handle hit
        queue_free()
```

### Area Projectile (Overlap)

Good for persistent volumes. For fast movers, upgrade with ShapeCast:

```gdscript
extends Node2D

@export var speed := 1400.0
@onready var cast := $ShapeCast2D as ShapeCast2D

func _physics_process(delta: float) -> void:
    var motion := transform.x * speed * delta
    cast.target_position = cast.to_local(global_position + motion)
    cast.force_shapecast_update()
    if cast.is_colliding():
        # Handle hit
        queue_free()
        return
    global_position += motion
```

### Tradeoffs

| Type | Speed | Accuracy | Bounce/Ricochet | Performance |
|------|-------|----------|-----------------|-------------|
| Hitscan ray | Instant | Perfect | Easy (bounce normal) | Cheapest |
| Kinematic body | Any | Good (swept) | Manual | Medium |
| Area overlap | Moderate | Frame-delayed | N/A | Medium |
| ShapeCast sweep | Any | Good (thick) | Manual | Most expensive |

---

## Hitbox/Hurtbox System

### Architecture

- **Hitbox** (Area2D): Emits damage intent. On player hitbox layer, scans enemy hurtbox layer.
- **Hurtbox** (Area2D): Validates and accepts damage. On enemy hurtbox layer.
- **Health** (Node): Mutates HP, emits signals.

```gdscript
# attack_data.gd
class_name AttackData
extends Resource
@export var damage := 1
@export var hitstun := 0.1
@export var i_frames := 0.2
@export var source_team := 0
```

```gdscript
# hurtbox_2d.gd
class_name Hurtbox2D
extends Area2D

signal accepted_hit(attack: AttackData)
var i_frames_left := 0.0

func _physics_process(delta: float) -> void:
    i_frames_left = max(i_frames_left - delta, 0.0)

func receive_hit(attack: AttackData) -> void:
    if attack.source_team == team or i_frames_left > 0.0:
        return
    i_frames_left = attack.i_frames
    accepted_hit.emit(attack)
```

```gdscript
# hitbox_2d.gd
class_name Hitbox2D
extends Area2D

@export var attack: AttackData

func _ready() -> void:
    area_entered.connect(_on_area_entered)

func _on_area_entered(area: Area2D) -> void:
    var hurtbox := area as Hurtbox2D
    if hurtbox:
        hurtbox.receive_hit(attack)
```

### Layer Strategy

- Player hitbox layer (8) scans enemy hurtbox layer (7)
- Enemy hitbox layer (9) scans player hurtbox layer (6)
- Bodies on separate layers from hitboxes/hurtboxes

---

## Moving Platforms

Use `AnimatableBody2D/3D` with `sync_to_physics = true`.

```gdscript
extends AnimatableBody3D

func _ready() -> void:
    sync_to_physics = true
    # Drive with AnimationPlayer or Tween
```

### CharacterBody Platform Tracking

- `platform_floor_layers`: Which layers count as rideable
- `platform_on_leave`: `ADD_VELOCITY` (full carry), `ADD_UPWARD_VELOCITY` (stable jump height), `DO_NOTHING`
- Platform velocity appears as first slide collision if it caused the contact

### Falling Platforms

Timer → disable collision or switch to RigidBody:

```gdscript
func _on_body_entered(_body: Node2D) -> void:
    await get_tree().create_timer(0.5).timeout
    # Option A: Disable
    collision_layer = 0
    # Option B: Convert to falling rigid body
```

---

## Environmental Interactions

### Gravity Zones (Water/Buoyancy)

```gdscript
# Water Area3D setup
extends Area3D
func _ready() -> void:
    gravity_space_override = Area3D.SPACE_OVERRIDE_REPLACE
    gravity_direction = Vector3.UP
    gravity = 2.0  # Weak upward
    linear_damp_space_override = Area3D.SPACE_OVERRIDE_REPLACE
    linear_damp = 5.0
```

CharacterBody samples via `get_gravity()` automatically. No enter/exit bookkeeping needed.

### Conveyor Belts

```gdscript
extends StaticBody2D
func _ready() -> void:
    constant_linear_velocity = Vector2.RIGHT * 160.0
```

Only affects RigidBody contacts. CharacterBody must implement traction in script.

### Bouncy Surfaces

```gdscript
var mat := PhysicsMaterial.new()
mat.bounce = 0.8
mat.absorbent = false  # true = subtracts bounciness from colliding object
physics_material_override = mat
```

---

## Game Feel Timing

### Use Float Countdowns, Not Timers

```gdscript
class_name GraceWindows
extends RefCounted

var coyote_left := 0.0
var jump_buffer_left := 0.0
var dash_lock_left := 0.0
var i_frames_left := 0.0

func tick(delta: float) -> void:
    coyote_left = max(coyote_left - delta, 0.0)
    jump_buffer_left = max(jump_buffer_left - delta, 0.0)
    dash_lock_left = max(dash_lock_left - delta, 0.0)
    i_frames_left = max(i_frames_left - delta, 0.0)
```

Why not Timer nodes: Timers update once per frame/tick, very short timers (<0.05s) can end inconsistently, and `SceneTreeTimer` processes after nodes. Custom countdowns in `_physics_process()` are deterministic and frame-perfect.

### Top-Down 2D

`Input.get_vector()` already normalizes diagonals (circular deadzone). Use directly as intention vector:

```gdscript
var input := Input.get_vector("move_left", "move_right", "move_up", "move_down")
velocity = velocity.move_toward(input * max_speed, accel * delta)
move_and_slide()
```
