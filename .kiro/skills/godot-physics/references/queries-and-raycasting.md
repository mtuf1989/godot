# Raycasting and Physics Queries

## When to Use Which

| Tool | Best For |
|------|----------|
| `RayCast2D/3D` node | Persistent per-frame checks (ground detection, line of sight) |
| `ShapeCast2D/3D` node | Persistent swept checks (wider ground detection, melee arcs) |
| `PhysicsDirectSpaceState` | One-off queries, multiple queries per frame, custom logic |

## RayCast Nodes

Evaluate once per physics tick. Cache result until next tick.

```gdscript
@onready var ray := $RayCast3D

func _physics_process(_delta: float) -> void:
    if ray.is_colliding():
        var point := ray.get_collision_point()
        var normal := ray.get_collision_normal()
        var collider := ray.get_collider()
```

### Key Properties

- `target_position`: Direction and length of the ray (local space)
- `collision_mask`: Which layers to scan
- `collide_with_areas` / `collide_with_bodies`: Filter by type
- `hit_from_inside` (3D): Reports hit if ray starts inside a shape. Returns zero normal.
- `hit_back_faces` (3D): Allows hits on back faces of concave/heightmap shapes.

### force_raycast_update()

Forces immediate evaluation. Use sparingly — for correctness-sensitive edge cases after moving the cast or reconfiguring masks. Not a blanket "always now" policy.

### Exclusions

```gdscript
ray.add_exception(self)  # Exclude self
ray.clear_exceptions()   # Reset
```

---

## ShapeCast Nodes

Sweep a shape along `target_position`. More expensive than rays but detects multiple colliders and has thickness.

### Use Cases

- Wide ground detection (wider than a point ray)
- Melee arc detection
- Ledge detection
- Bullet thickness for rockets/shells
- Immediate overlap test (set `target_position = Vector3.ZERO`, force update)

### Key Methods

```gdscript
@onready var cast := $ShapeCast3D

func _physics_process(_delta: float) -> void:
    if cast.is_colliding():
        var count := cast.get_collision_count()
        for i in count:
            var collider := cast.get_collider(i)
            var point := cast.get_collision_point(i)
            var normal := cast.get_collision_normal(i)
        
        # Safe/unsafe fractions for custom kinematic solvers
        var safe := cast.get_closest_collision_safe_fraction()
        var unsafe := cast.get_closest_collision_unsafe_fraction()
```

### Important Details

- `max_results`: Limits processing time for multi-hit queries
- Multiple hits are "all colliders at the earliest impact position," NOT a chronological stream along the path
- Safe fraction = how far shape can move without collision. Unsafe = how far to trigger one.
- For long-distance sampling, substep the movement rather than one long cast.

### force_shapecast_update()

Same as raycast — forces immediate evaluation. Call `force_update_transform()` first if you moved the cast in the same frame:

```gdscript
cast.global_position = new_position
cast.force_update_transform()
cast.force_shapecast_update()
```

---

## PhysicsDirectSpaceState (Direct Queries)

### Getting the Space State

```gdscript
# Only safe in _physics_process()!
var space := get_world_3d().direct_space_state
# or
var space := get_world_2d().direct_space_state
```

**Timing rule**: Only access during `_physics_process()`. Outside that callback, the space may be locked (especially with threaded physics).

### intersect_ray()

```gdscript
func hitscan_fire(from: Vector3, to: Vector3) -> Dictionary:
    var query := PhysicsRayQueryParameters3D.create(from, to)
    query.collision_mask = CollisionLayers.mask_of([1, 4, 7])  # world, enemy body, enemy hurtbox
    query.exclude = [get_rid()]  # Exclude self
    query.collide_with_areas = false
    query.collide_with_bodies = true
    return get_world_3d().direct_space_state.intersect_ray(query)

# Result dictionary (empty if no hit):
# { "position": Vector3, "normal": Vector3, "collider": Object,
#   "collider_id": int, "rid": RID, "shape": int, "face_index": int }
```

### intersect_shape()

Area overlap test. Ignores `motion` field.

```gdscript
func check_overlap(shape: Shape3D, at: Transform3D) -> Array[Dictionary]:
    var params := PhysicsShapeQueryParameters3D.new()
    params.shape = shape
    params.transform = at
    params.collision_mask = 0xFFFFFFFF
    params.collide_with_areas = false
    params.collide_with_bodies = true
    return get_world_3d().direct_space_state.intersect_shape(params, 32)
```

### cast_motion()

Motion-aware sweep. Returns `[safe_fraction, unsafe_fraction]`.

```gdscript
func sweep_test(shape: Shape3D, from: Transform3D, motion: Vector3) -> PackedFloat32Array:
    var params := PhysicsShapeQueryParameters3D.new()
    params.shape = shape
    params.transform = from
    params.motion = motion
    params.collision_mask = CollisionLayers.mask_of([1])
    return get_world_3d().direct_space_state.cast_motion(params)
```

**Important**: `cast_motion()` ignores shapes you're already intersecting at the start. Use `intersect_shape()` first for initial penetration handling, then `cast_motion()` for the sweep.

### get_rest_info()

Returns nearest contact metadata (point, normal, collider RID, linear velocity). Also ignores motion.

### Exclusion Best Practices

- Use RID exclusions for a handful of specific self-collision suppressions (shooter, owner, held object)
- If exclusion set becomes large, use collision masks instead — more efficient
- `exclude` returns a copied array; if you mutate it, assign it back

---

## Common Patterns

### Mouse Picking (3D)

```gdscript
# In _physics_process() for thread safety
func pick_at(screen_pos: Vector2) -> Dictionary:
    var origin := camera.project_ray_origin(screen_pos)
    var direction := camera.project_ray_normal(screen_pos)
    var to := origin + direction * 5000.0
    
    var query := PhysicsRayQueryParameters3D.create(origin, to)
    query.collision_mask = CollisionLayers.mask_of([1, 10, 11])
    query.collide_with_areas = true
    return get_world_3d().direct_space_state.intersect_ray(query)
```

### Line-of-Sight Check

```gdscript
func has_line_of_sight(from: Vector3, to: Vector3, exclude_self: RID) -> bool:
    var query := PhysicsRayQueryParameters3D.create(from, to)
    query.collision_mask = CollisionLayers.bit(CollisionLayers.WORLD)
    query.exclude = [exclude_self]
    return get_world_3d().direct_space_state.intersect_ray(query).is_empty()
```

### Hitscan with Ricochet

```gdscript
func fire_ricochet(origin: Vector3, direction: Vector3, max_range: float, max_bounces: int) -> Array[Dictionary]:
    var segments: Array[Dictionary] = []
    var current_origin := origin
    var current_dir := direction.normalized()
    var remaining := max_range
    
    for bounce in max_bounces + 1:
        var to := current_origin + current_dir * remaining
        var query := PhysicsRayQueryParameters3D.create(current_origin, to)
        query.collision_mask = CollisionLayers.mask_of([1, 4, 7])
        var hit := get_world_3d().direct_space_state.intersect_ray(query)
        
        if hit.is_empty():
            segments.append({"from": current_origin, "to": to, "hit": false})
            break
        
        segments.append({"from": current_origin, "to": hit["position"], "hit": true, "normal": hit["normal"]})
        remaining -= current_origin.distance_to(hit["position"])
        
        if bounce >= max_bounces:
            break
        current_dir = current_dir.bounce(hit["normal"]).normalized()
        current_origin = hit["position"] + hit["normal"] * 0.02
    
    return segments
```

---

## Timing and Safety

| Context | Safe to Query? | Notes |
|---------|---------------|-------|
| `_physics_process()` | ✅ Yes | Intended callback for all physics queries |
| `_process()` | ⚠️ Stale | Space state reflects last physics tick, not current frame |
| `_ready()` | ⚠️ Risky | Physics world may not have run a step yet. Use `force_raycast_update()` or wait for first `physics_frame` |
| `_input()` | ❌ No | Space may be locked. Defer to `_physics_process()` |
| Worker threads | ❌ No | Scene-tree physics queries are not thread-safe |

### Querying in _ready()

If you need startup collision data:
- For RayCast/ShapeCast nodes: move node, call `force_update_transform()`, then `force_raycast_update()`
- For space state queries: connect to `get_tree().physics_frame` and query on first signal
- Area overlap lists are NOT populated until after the first physics step
