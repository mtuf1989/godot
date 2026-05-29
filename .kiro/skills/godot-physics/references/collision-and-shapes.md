# Collision System and Shapes

## Collision Layers and Masks

**Layer** = "I am on this channel" (what I am).
**Mask** = "I scan for these channels" (what I look for).

```
A collides with B := (A.mask & B.layer) != 0  OR  (B.mask & A.layer) != 0
```

For Area overlap signals, detection is one-sided from the monitoring Area's perspective.

### Recommended Layer Template

| Layer | Platformer | Top-Down Shooter | 3D Action |
|-------|-----------|-----------------|-----------|
| 1 | World solid | World solid | World solid |
| 2 | One-way platforms | Half-cover | Climbable |
| 3 | Player body | Player body | Player body |
| 4 | Enemy body | Enemy body | Enemy body |
| 5 | NPC body | Neutral/civilian | NPC |
| 6 | Player hurtbox | Player hurtbox | Player hurtbox |
| 7 | Enemy hurtbox | Enemy hurtbox | Enemy hurtbox |
| 8 | Player hitbox | Player projectile | Player weapon |
| 9 | Enemy hitbox | Enemy projectile | Enemy weapon |
| 10 | Pickup | Pickup | Pickup |
| 11 | Interactable | Interactable | Interactable |
| 12 | Hazard | Hazard | Hazard |
| 13 | AI sensor | AI sensor | AI sensor |
| 14 | Camera bounds | Camera bounds | Camera bounds |
| 16 | Moving platforms | Doors/movers | Dynamic movers |
| 18 | Ghost/dodge state | Shield/parry | Dodge/phase |

### Layer Utility Helper

```gdscript
class_name CollisionLayers
extends RefCounted

const WORLD := 1
const ONE_WAY := 2
const PLAYER_BODY := 3
const ENEMY_BODY := 4
const PLAYER_HURTBOX := 6
const ENEMY_HURTBOX := 7
const PLAYER_HITBOX := 8
const ENEMY_HITBOX := 9
const PICKUP := 10
const MOVING_PLATFORM := 16
const GHOST := 18

static func bit(layer_number: int) -> int:
    assert(layer_number >= 1 and layer_number <= 32)
    return 1 << (layer_number - 1)

static func mask_of(layer_numbers: Array[int]) -> int:
    var result := 0
    for n in layer_numbers:
        result |= bit(n)
    return result
```

### Runtime Mutation Rules

- Compute final integer and assign `collision_layer` / `collision_mask` once — don't call `set_collision_mask_value()` per-bit in a loop.
- On Jolt, unique (layer, mask) combinations are cached. Reuse a small set of presets (normal / invulnerable / ghost / cinematic) rather than generating ad-hoc combinations.
- Area overlap lists update once per physics step, not immediately after toggling. Schedule changes in `_physics_process()`.

---

## Shape Types

### 2D Performance Order (fastest → slowest)

1. **CircleShape2D** — simplest, cheapest
2. **RectangleShape2D** — good for world and coarse actors
3. **CapsuleShape2D** — better character feel
4. **ConvexPolygonShape2D** — when primitives fail
5. **ConcavePolygonShape2D** — static world only

### 3D Performance Order (fastest → slowest)

1. **SphereShape3D** — cheapest
2. **BoxShape3D** — good for world geometry
3. **CapsuleShape3D** — best general character shape
4. **CylinderShape3D** — documented bugs, prefer capsule
5. **ConvexPolygonShape3D** — when primitives fail
6. **HeightMapShape3D** — terrain only, no overhangs
7. **ConcavePolygonShape3D** — static world only, slowest

### Critical Rules

- **ConcavePolygonShape**: Static-only. "Will likely not behave well" on CharacterBody or dynamic RigidBody. Fast small bodies can clip through it.
- **CylinderShape3D**: "Several known bugs." Use CapsuleShape3D or BoxShape3D instead. Jolt improves stability but capsules remain safer.
- **HeightMapShape3D**: Terrain-specific. No overhangs/caves. Supports holes via very low values.
- **SeparationRayShape2D/3D**: Not general collision — it's a traversal aid. Moves its endpoint to collision point. Use for stair-stepping and ledge bias.

### Scaling Shapes — THE RULE

**Never scale collision shapes via node transform.** Non-uniform scale breaks collision detection entirely. Even uniform scale has caveats.

Correct approach:
```gdscript
# Resize the shape resource, not the node
static func resize_box_3d(cs: CollisionShape3D, new_size: Vector3) -> void:
    cs.shape = cs.shape.duplicate()  # Don't affect other instances
    (cs.shape as BoxShape3D).size = new_size
```

### Compound Shapes

- 2-6 primitives beats one dense convex hull for characters and props
- Use single convex hull only when primitives would distort gameplay collision
- Multiple convex pieces only for true concave dynamic interaction
- Reserve tri-mesh concave for baked static world collision

---

## One-Way Collision

One-way collision is a contact-normal direction test. The engine checks the dot product between the shape's local Y direction and the contact normal.

### Setup

- `CollisionShape2D.one_way_collision = true`
- `one_way_collision_margin`: Tolerance band for high-speed contacts. Not a substitute for CCD.

### Drop-Through Pattern

```gdscript
class_name OneWayDropController2D
extends RefCounted

var pass_through_layer: int = CollisionLayers.ONE_WAY
var drop_duration: float = 0.12
var _active: bool = false
var _timer: float = 0.0
var _saved_mask: int = 0
var _saved_platform_floor_layers: int = 0

func begin_drop(body: CharacterBody2D) -> void:
    if _active:
        return
    _active = true
    _timer = drop_duration
    _saved_mask = body.collision_mask
    _saved_platform_floor_layers = body.platform_floor_layers
    
    body.platform_on_leave = CharacterBody2D.PLATFORM_ON_LEAVE_DO_NOTHING
    var one_way_bit := CollisionLayers.bit(pass_through_layer)
    body.collision_mask &= ~one_way_bit
    body.platform_floor_layers &= ~one_way_bit
    body.floor_snap_length = 0.0
    body.velocity.y = max(body.velocity.y, 48.0)

func physics_update(body: CharacterBody2D, delta: float) -> void:
    if not _active:
        return
    _timer -= delta
    if _timer <= 0.0:
        body.collision_mask = _saved_mask
        body.platform_floor_layers = _saved_platform_floor_layers
        _active = false
```

---

## Area2D/Area3D

### monitoring vs monitorable

- `monitoring = true`: This Area looks for overlaps (emits signals)
- `monitorable = true`: Other monitoring Areas can detect this Area

Turn off what you don't need. A passive trigger zone doesn't need to be monitorable.

### Gravity and Damping Overrides

```gdscript
# Water zone
extends Area3D

func _ready() -> void:
    gravity_space_override = Area3D.SPACE_OVERRIDE_REPLACE
    gravity_direction = Vector3.UP
    gravity = 2.0  # Weak upward buoyancy
    linear_damp_space_override = Area3D.SPACE_OVERRIDE_REPLACE
    linear_damp = 5.0
```

`CharacterBody` can sample the resulting gravity via `get_gravity()` — no custom enter/exit bookkeeping needed.

### Performance Rules

- Overlap lists update once per physics step, not immediately
- Dense Area-vs-Area graphs spike CPU and memory
- Prefer one player collector Area over hundreds of pickup Areas
- On Jolt: `Area3D` does NOT detect `StaticBody3D` by default. Enable via `Physics > Jolt Physics 3D > Simulation > Areas Detect Static Bodies` if needed.
- `body_entered` fires per-shape. Multiple shapes on one body = multiple signals.
