# PhysicsServer Direct API

## When to Use

The PhysicsServer direct API bypasses scene tree nodes entirely, managing physics objects via lightweight RID handles. Use when:

- **Thousands of identical physics objects** (bullet hell, particle-like bodies)
- **Minimal per-entity logic** — objects don't need scripts, signals, or complex scene integration
- **Maximum performance** — eliminates Node overhead (memory, callbacks, tree traversal)
- **Custom kinematic solvers** — need direct control over body state without Node abstraction

Do NOT use for:
- Normal gameplay entities that benefit from scene tree integration
- Objects that need editor visibility, inspector properties, or signal connections
- Small numbers of bodies where Node overhead is negligible

---

## Performance Rationale

Each Node carries overhead:
- Object identity, properties, parent/child management, signals, groups
- `_physics_process()` callback dispatch (even if empty)
- Node-to-Server communication layer (Variant conversion, virtual calls)
- Reference counting and lifecycle management

RIDs are opaque integer handles. The PhysicsServer stores body data in optimized internal structures with better cache locality. For 5,000+ bodies, the difference is significant.

---

## Lifecycle: Create → Configure → Use → Free

### 1. Get or Create a Space

```gdscript
# Use the scene's existing space (most common)
var space_rid: RID = get_world_3d().space

# Or create an isolated space (rare — for separate simulation)
var custom_space := PhysicsServer3D.space_create()
PhysicsServer3D.space_set_active(custom_space, true)
```

### 2. Create a Shape

```gdscript
# Shapes are shared — create once, reuse across many bodies
var sphere_shape := PhysicsServer3D.sphere_shape_create()
PhysicsServer3D.shape_set_data(sphere_shape, 0.1)  # radius

var box_shape := PhysicsServer3D.box_shape_create()
PhysicsServer3D.shape_set_data(box_shape, Vector3(0.5, 0.5, 0.5))  # half-extents

var capsule_shape := PhysicsServer3D.capsule_shape_create()
PhysicsServer3D.shape_set_data(capsule_shape, {"radius": 0.3, "height": 1.0})
```

### 3. Create and Configure a Body

```gdscript
var body_rid := PhysicsServer3D.body_create()
PhysicsServer3D.body_set_mode(body_rid, PhysicsServer3D.BODY_MODE_KINEMATIC)
PhysicsServer3D.body_set_space(body_rid, space_rid)
PhysicsServer3D.body_add_shape(body_rid, sphere_shape, Transform3D(), false)
PhysicsServer3D.body_set_collision_layer(body_rid, 1 << 2)  # Layer 3
PhysicsServer3D.body_set_collision_mask(body_rid, 1 << 0 | 1 << 1)  # Scan layers 1,2
```

### 4. Update State Each Tick

```gdscript
# Set transform directly
PhysicsServer3D.body_set_state(body_rid, PhysicsServer3D.BODY_STATE_TRANSFORM, new_transform)

# Set velocity (for kinematic or rigid bodies)
PhysicsServer3D.body_set_state(body_rid, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, velocity)
```

### 5. Free When Done

```gdscript
# CRITICAL: Always free RIDs to prevent memory leaks
PhysicsServer3D.free_rid(body_rid)
PhysicsServer3D.free_rid(sphere_shape)  # Only after all bodies using it are freed
# PhysicsServer3D.free_rid(custom_space)  # Only if you created a custom space
```

---

## Body Modes

| Mode | Constant | Behavior |
|------|----------|----------|
| Static | `BODY_MODE_STATIC` | Immovable, no velocity |
| Kinematic | `BODY_MODE_KINEMATIC` | You control transform directly |
| Rigid | `BODY_MODE_RIGID` | Physics-driven, responds to forces |
| Rigid Linear | `BODY_MODE_RIGID_LINEAR` | Rigid but no rotation |

---

## Collision Detection for RID Bodies

### Using body_test_motion()

```gdscript
var params := PhysicsTestMotionParameters3D.new()
params.from = current_transform
params.motion = velocity * delta

var result := PhysicsTestMotionResult3D.new()
var collided := PhysicsServer3D.body_test_motion(body_rid, params, result)

if collided:
    var collision_point := result.get_collision_point()
    var collision_normal := result.get_collision_normal()
    var safe_fraction := result.get_collision_safe_fraction()
```

### Using Direct Space State

```gdscript
# Query from the space the body lives in
var space_state := PhysicsServer3D.space_get_direct_state(space_rid)
var query := PhysicsRayQueryParameters3D.create(from, to)
query.exclude = [body_rid]  # Exclude self
var hit := space_state.intersect_ray(query)
```

---

## Bullet Pool Pattern

Complete example for managing thousands of kinematic projectiles without nodes:

```gdscript
class_name BulletPool3D
extends Node

const MAX_BULLETS := 5000
const BULLET_SPEED := 50.0
const BULLET_LIFETIME := 4.0
const BULLET_RADIUS := 0.05

var _space_rid: RID
var _shape_rid: RID
var _body_rids: Array[RID] = []
var _transforms: Array[Transform3D] = []
var _velocities: Array[Vector3] = []
var _lifetimes: Array[float] = []
var _active: Array[bool] = []
var _next_index: int = 0

func _ready() -> void:
    _space_rid = get_world_3d().space
    
    # Single shared shape for all bullets
    _shape_rid = PhysicsServer3D.sphere_shape_create()
    PhysicsServer3D.shape_set_data(_shape_rid, BULLET_RADIUS)
    
    # Pre-allocate pool
    _body_rids.resize(MAX_BULLETS)
    _transforms.resize(MAX_BULLETS)
    _velocities.resize(MAX_BULLETS)
    _lifetimes.resize(MAX_BULLETS)
    _active.resize(MAX_BULLETS)
    
    for i in MAX_BULLETS:
        var body := PhysicsServer3D.body_create()
        PhysicsServer3D.body_set_mode(body, PhysicsServer3D.BODY_MODE_KINEMATIC)
        PhysicsServer3D.body_set_space(body, _space_rid)
        PhysicsServer3D.body_add_shape(body, _shape_rid, Transform3D(), true)  # Start disabled
        PhysicsServer3D.body_set_collision_layer(body, 1 << 2)
        PhysicsServer3D.body_set_collision_mask(body, 1 << 0 | 1 << 3)
        _body_rids[i] = body
        _transforms[i] = Transform3D()
        _velocities[i] = Vector3.ZERO
        _lifetimes[i] = 0.0
        _active[i] = false

func spawn(position: Vector3, direction: Vector3) -> void:
    # Find inactive slot
    var idx := -1
    for i in MAX_BULLETS:
        var check := (_next_index + i) % MAX_BULLETS
        if not _active[check]:
            idx = check
            break
    if idx == -1:
        return  # Pool exhausted
    
    _next_index = (idx + 1) % MAX_BULLETS
    _transforms[idx] = Transform3D(Basis(), position)
    _velocities[idx] = direction.normalized() * BULLET_SPEED
    _lifetimes[idx] = BULLET_LIFETIME
    _active[idx] = true
    
    # Enable shape and set initial state
    PhysicsServer3D.body_set_shape_disabled(_body_rids[idx], 0, false)
    PhysicsServer3D.body_set_state(_body_rids[idx], PhysicsServer3D.BODY_STATE_TRANSFORM, _transforms[idx])

func _physics_process(delta: float) -> void:
    for i in MAX_BULLETS:
        if not _active[i]:
            continue
        
        _lifetimes[i] -= delta
        if _lifetimes[i] <= 0.0:
            _deactivate(i)
            continue
        
        # Move
        _transforms[i].origin += _velocities[i] * delta
        PhysicsServer3D.body_set_state(
            _body_rids[i],
            PhysicsServer3D.BODY_STATE_TRANSFORM,
            _transforms[i]
        )

func _deactivate(idx: int) -> void:
    _active[idx] = false
    PhysicsServer3D.body_set_shape_disabled(_body_rids[idx], 0, true)

func _exit_tree() -> void:
    for body in _body_rids:
        PhysicsServer3D.free_rid(body)
    PhysicsServer3D.free_rid(_shape_rid)
```

### Rendering

Pair with `MultiMeshInstance3D` for visual representation. Update MultiMesh transforms from `_transforms` array each frame. This completely decouples physics from rendering.

---

## Thread Safety Rules

1. **`_physics_process()` is the safe callback** for PhysicsServer state modifications
2. **Don't modify physics state from `_process()` or worker threads** — space may be locked
3. **`PhysicsServer3DWrapMT`** wraps the server for thread-safe access, queuing operations
4. **`body_get_direct_state()`** is only accessible outside the active physics step
5. **Contact data** is collected during physics step, flushed via `flush_queries()` before main thread access
6. **For batch preparation**: do math on worker threads, marshal finalized queries to `_physics_process()`

### Force Integration Callback

For custom per-body physics logic without nodes:

```gdscript
PhysicsServer3D.body_set_force_integration_callback(body_rid, _on_integrate)

func _on_integrate(state: PhysicsDirectBodyState3D, _userdata: Variant) -> void:
    # Called during physics step — safe to modify state here
    state.linear_velocity = state.linear_velocity.limit_length(max_speed)
```

---

## Safety Guidelines

### Memory Leaks (Orphan RIDs)

- Every `*_create()` call MUST have a matching `free_rid()` call
- Free bodies before freeing their shapes
- Free shapes only after all bodies using them are freed
- Use `_exit_tree()` or `_notification(NOTIFICATION_PREDELETE)` for cleanup
- If a manager creates RIDs, that manager owns freeing them

### Common Mistakes

- Forgetting to call `body_set_space()` — body exists but isn't simulated
- Forgetting to free RIDs on scene exit — memory leak
- Creating a new shape per body when one shared shape would work
- Calling `body_get_direct_state()` during active physics step — returns error
- Using custom space but forgetting `space_set_active(space, true)`

---

## 2D Equivalent

The 2D API mirrors 3D exactly:
- `PhysicsServer2D.body_create()`, `circle_shape_create()`, etc.
- `PhysicsServer2D.body_set_state(rid, BODY_STATE_TRANSFORM, Transform2D(...))`
- Same lifecycle: create → configure → use → free
- Same thread safety rules apply
