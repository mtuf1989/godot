# Body Types and Movement

## CharacterBody2D/3D

### move_and_slide() Mental Model

`move_and_slide()` is a multi-iteration motion pipeline, not a single sweep. It:
1. Computes `motion = velocity * delta`
2. Iterates up to `max_slides` times
3. Calls `move_and_collide()` internally with `safe_margin`
4. Classifies each contact as floor/wall/ceiling using `up_direction` + `floor_max_angle`
5. Rewrites remaining motion using `slide()`
6. Runs a floor-snap pass if previously grounded and not moving upward
7. Enables `recovery_as_collision` so depenetration contacts participate in state classification

### Property Reference

```gdscript
# 3D baseline setup
func _ready() -> void:
    motion_mode = CharacterBody3D.MOTION_MODE_GROUNDED
    up_direction = Vector3.UP
    floor_max_angle = deg_to_rad(45.0)
    floor_snap_length = 0.25
    floor_stop_on_slope = true
    floor_block_on_wall = true
    floor_constant_speed = false
    slide_on_ceiling = true
    max_slides = 6
    safe_margin = 0.001
```

```gdscript
# 2D baseline setup
func _ready() -> void:
    motion_mode = CharacterBody2D.MOTION_MODE_GROUNDED
    up_direction = Vector2.UP
    floor_max_angle = deg_to_rad(45.0)
    floor_snap_length = 6.0
    floor_stop_on_slope = true
    floor_block_on_wall = true
    floor_constant_speed = false
    slide_on_ceiling = true
    max_slides = 6
    safe_margin = 0.08
```

### Key Properties Explained

- **`velocity`**: Requested motion vector. `move_and_slide()` both consumes and mutates it.
- **`motion_mode`**: `GROUNDED` enables floor/wall/ceiling classification; `FLOATING` treats all contacts as walls.
- **`floor_snap_length`**: How far downward the solver probes to maintain floor contact. Only applies when previously grounded and not moving upward.
- **`safe_margin`**: Pre-motion recovery margin. 3D default `0.001`, 2D default `0.08`. Higher = more consistent detection, lower = more precision.
- **`floor_constant_speed`**: When true, maintains constant speed on slopes (no slowdown uphill, no speedup downhill).
- **`platform_floor_layers`**: Which collision layers count as rideable moving platforms.
- **`platform_on_leave`**: `ADD_VELOCITY` (default), `ADD_UPWARD_VELOCITY`, or `DO_NOTHING`.

### Post-Move State Queries

```gdscript
# These report state from the LAST move_and_slide() call
is_on_floor()          # Contacted a floor surface
is_on_wall()           # Contacted a wall surface
is_on_ceiling()        # Contacted a ceiling surface
is_on_wall_only()      # On wall but NOT on floor
get_floor_normal()     # Normal of the floor contact
get_floor_angle()      # Angle between floor normal and up_direction
get_wall_normal()      # Normal of the wall contact
get_real_velocity()    # Actual solved motion (differs from velocity on slopes)
get_last_motion()      # Final motion segment of multi-segment solve
get_platform_velocity() # Velocity of the platform being ridden
get_slide_collision_count()  # Number of collisions this solve
get_slide_collision(i)       # KinematicCollision at index i
```

### apply_floor_snap()

Call manually when you need to force floor attachment outside the normal `move_and_slide()` flow. Does nothing if already on floor. Useful after:
- Landing detection where you want immediate snap
- Ledge transitions where the auto-snap didn't fire

```gdscript
func _physics_process(delta: float) -> void:
    var was_on_floor := is_on_floor()
    # ... update velocity ...
    move_and_slide()
    
    # Manual snap for ledge smoothing
    if not is_on_floor() and was_on_floor and velocity.dot(up_direction) <= 0.0:
        apply_floor_snap()
```

### move_and_collide() for Custom Response

Use when you need full policy control (ricochet, bounce, custom step logic):

```gdscript
# Ricochet projectile
func move_with_ricochet(delta: float) -> void:
    var remaining := velocity * delta
    var bounces := 0
    while bounces < 4 and remaining.length_squared() > 0.000001:
        var hit := move_and_collide(remaining)
        if hit == null:
            break
        velocity = velocity.bounce(hit.get_normal())
        remaining = hit.get_remainder().bounce(hit.get_normal())
        bounces += 1
```

---

## RigidBody2D/3D

### Force Application

| Method | When to Use |
|--------|-------------|
| `apply_force(force, position)` | Continuous push (engines, wind) — call every frame |
| `apply_central_force(force)` | Same but at center of mass (no torque) |
| `apply_impulse(impulse, position)` | One-shot event (explosion, hit, recoil) |
| `apply_central_impulse(impulse)` | Same but at center of mass |
| `add_constant_force(force)` | Persistent force that stays until removed |

**Impulses are time-independent** — never apply them every frame (becomes frame-rate-dependent force).

### _integrate_forces()

Called during physics processing. Gives safe access to `PhysicsDirectBodyState2D/3D`.

```gdscript
extends RigidBody3D

@export var max_speed: float = 12.0
@export var thrust_force: float = 20.0

func _ready() -> void:
    custom_integrator = false  # Keep default gravity/damping
    lock_rotation = true
    can_sleep = false

func _integrate_forces(state: PhysicsDirectBodyState3D) -> void:
    # Clamp speed (safe — runs during physics step)
    state.linear_velocity = state.linear_velocity.limit_length(max_speed)
```

With `custom_integrator = true`, you own ALL motion — gravity and damping are disabled:

```gdscript
func _integrate_forces(state: PhysicsDirectBodyState3D) -> void:
    # You must apply gravity yourself
    state.linear_velocity += get_gravity() * state.step
    # Apply thrust
    if Input.is_action_pressed("thrust"):
        state.linear_velocity += -global_transform.basis.z * thrust_force / mass * state.step
    state.linear_velocity = state.linear_velocity.limit_length(max_speed)
```

### Continuous Collision Detection

**2D** — three modes:
- `CCD_MODE_DISABLED`: Default, discrete stepping
- `CCD_MODE_CAST_RAY`: Faster, less precise — good for thin fast projectiles
- `CCD_MODE_CAST_SHAPE`: Slower, most precise — good for bodies with volume

**3D** — boolean `continuous_cd`:
- With Jolt: maps to `LinearCast` motion quality internally
- No ray-vs-shape choice exposed at node level

CCD is NOT a complete tunneling solution. Also consider: thicker colliders, higher tick rate, limiting max velocity.

### Sleeping and Stability

- `can_sleep = true` (default): Body sleeps when at rest. Essential for performance.
- Add small `linear_damp` to chattery props that oscillate near sleep threshold.
- Don't disable sleeping globally — it's your primary performance tool for clutter.
- Jolt defaults: `sleep_velocity_threshold = 0.03 m/s`, `sleep_time_threshold = 0.5 s`

### Contact Monitoring

```gdscript
# Required setup for body_entered/exited signals
contact_monitor = true
max_contacts_reported = 4  # Must be > 0

# Jolt caveat: frozen KINEMATIC bodies need "Generate All Kinematic Contacts" enabled
```

---

## StaticBody2D/3D

Immovable collision. If you manually move it, it teleports (no swept motion, no velocity imparted).

### Conveyor Belts and Rotating Surfaces

```gdscript
extends StaticBody3D

@export var belt_velocity: Vector3 = Vector3.RIGHT * 2.0

func _ready() -> void:
    constant_linear_velocity = belt_velocity
    var mat := PhysicsMaterial.new()
    mat.friction = 1.0
    mat.rough = true  # Uses this body's friction instead of minimum of pair
    physics_material_override = mat
```

**Important**: `CharacterBody` is NOT affected by `PhysicsMaterial`. Friction/bounce only affect `RigidBody` contacts. Character traction must be authored in your controller script.

---

## AnimatableBody2D/3D

Use for manually moved or animated bodies that must affect other physics bodies in their path.

- Engine estimates velocity from movement and applies it to touching bodies
- `sync_to_physics = true`: Synchronizes to physics frame — use with AnimationPlayer-driven movement
- This is the correct node for moving platforms, doors, pistons, elevators

```gdscript
# Moving platform setup
extends AnimatableBody3D

func _ready() -> void:
    sync_to_physics = true
    # Drive with AnimationPlayer or Tween targeting position/rotation
```

### CharacterBody Pushing RigidBody

CharacterBody doesn't automatically push RigidBody. Add a push bridge:

```gdscript
extends Node
class_name RigidBodyPushBridge3D

@export var impulse_scale: float = 0.35
@export var max_push_speed: float = 4.0

func apply_from(body: CharacterBody3D) -> void:
    for i in body.get_slide_collision_count():
        var c := body.get_slide_collision(i)
        var rb := c.get_collider() as RigidBody3D
        if rb == null:
            continue
        var relative := body.get_real_velocity() - c.get_collider_velocity()
        var tangent := relative.slide(c.get_normal())
        if tangent.is_zero_approx():
            continue
        var push := tangent.limit_length(max_push_speed) * impulse_scale * rb.mass
        rb.apply_central_impulse(push)
```

Call `push_bridge.apply_from(self)` after `move_and_slide()` in your character controller.
