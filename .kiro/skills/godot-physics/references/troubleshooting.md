# Physics Troubleshooting

## Diagnostic Table

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| Fast body passes through walls | Discrete stepping skips geometry | Enable CCD, thicken colliders, raise TPS |
| Character clips at high speed | Motion exceeds safe swept query | Keep in `_physics_process()`, use `move_and_slide()`, raise TPS |
| Bumps on flat TileMap floor | Edge normals from adjacent tile shapes | Merge into composite collider per island |
| Seams catch in 3D Jolt floors | Internal-edge mitigation only works within same body | Merge floor pieces into one static body |
| Stair-climbing jittery | Floor topology + snap insufficient | Add SeparationRayShape or script-level step solver |
| Scaled shape behaves wrong | Non-uniform scale breaks physics | Keep scale at identity, resize shape resource |
| Teleport leaves visual streak | Interpolation from old position | Call `reset_physics_interpolation()` after transform set |
| Camera micro-jitter | Mixing `_process` and `_physics_process` timelines | Camera in `_process()` reading interpolated target |
| Stacks wobble/explode | Under-converged solver | Raise TPS, use Jolt, tune velocity_steps |
| Resting clutter jitters forever | Sleep never triggers | Keep `can_sleep` enabled, add small `linear_damp` |
| Area3D doesn't see static geometry (Jolt) | Disabled by default for performance | Enable Areas Detect Static Bodies |
| Frozen kinematic body no contacts (Jolt) | Opt-in for those bodies | Enable Generate All Kinematic Contacts |
| Open-world physics breaks far from origin | Single-precision float limits | Use double precision build or origin shifting |

---

## Tunneling

### Cause

Fast-moving objects travel farther per physics step than the solver can resolve. At 360 km/h and 60 Hz, a body advances ~1.67 units per tick.

### Solutions (apply in order)

1. **Enable CCD** on the fast body
   - 2D: `CCD_MODE_CAST_SHAPE` (precise) or `CCD_MODE_CAST_RAY` (faster, less precise)
   - 3D: `continuous_cd = true` (Jolt maps to LinearCast internally)
2. **Thicken static colliders** — make walls/floors thick enough to catch fast movers
3. **Limit max velocity** — cap speed to what your geometry can handle
4. **Raise physics tick rate** — 120+ Hz for very fast objects
5. **Use raycast/shapecast ahead** — pre-check trajectory before moving

### CharacterBody Tunneling

`move_and_slide()` uses swept motion internally (`body_test_motion()`), so it's more resistant than naive teleport. But at extreme speeds or with very thin geometry, it can still miss. Raise TPS or pre-sweep.

---

## Stacked Objects Instability

### Cause

Solver can't converge in limited iterations. Worse with:
- Many stacked bodies
- Large mass ratios
- Thin contact surfaces
- Low tick rate

### Solutions

1. **Use Jolt** (3D) — Godot's troubleshooting docs explicitly recommend this
2. **Raise physics tick rate** — 120+ Hz
3. **Raise Jolt `velocity_steps`** (default 10, try 15-20 for heavy stacks)
4. **Reduce stack height** in gameplay design
5. **Enable sleeping** — let settled bodies stop simulating
6. **Normalize masses** — avoid 100:1 mass ratios in stacks
7. **Add small `linear_damp`** to reduce oscillation near rest

### Jolt Settings

```
jolt_physics_3d/simulation/velocity_steps = 10  # Raise first
jolt_physics_3d/simulation/position_steps = 2   # Raise second
```

---

## Tile-Edge Bumps / Ghost Collisions

### Cause

CharacterBody catches on seams between adjacent collision shapes. The edge normal points sideways, creating a false "wall" contact.

### Solutions

1. **Merge into composite collider** — replace per-tile shapes with one merged shape per island
2. **SeparationRayShape2D/3D** — adds step-up bias at character feet
3. **Adjust `safe_margin`** — slightly larger margin can smooth over micro-seams
4. **Slight shape overlap** — overlap adjacent shapes by a tiny amount

### Jolt 3D Advantage

Jolt uses active edge detection + enhanced internal edge removal. But this only works **within the same body**. Merge floor pieces into one static body to benefit.

---

## Jitter and Stutter

### Physics Jitter (mismatched tick/frame rate)

**Fix**: Enable physics interpolation. Test at 10 TPS to confirm.

### Camera Jitter

**Fix**: Camera in `_process()` with `physics_interpolation_mode = OFF`, following `get_global_transform_interpolated()`. Set `top_level = true`.

### RigidBody Jitter at Rest

**Causes**: Sleep threshold too aggressive, contact flickering, restitution causing micro-bounces.

**Fix**: Keep sleeping enabled, add small `linear_damp`, reduce `bounce` on resting surfaces.

### AnimationPlayer/Tween Jitter

**Cause**: Driving interpolated objects from idle-time processing.

**Fix**: 
- AnimationMixer: `ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS`
- Tween: `.set_process_mode(TWEEN_PROCESS_PHYSICS)`

---

## Scaled Collision Shapes

### The Problem

Non-uniform scale on parent breaks collision detection. Physics shapes use local parameter space (radius, size, height). Non-uniform scale changes support points and normals in ways that don't match primitive assumptions.

### The Rule

**Never scale collision shape nodes via transform.** Resize the shape resource directly.

```gdscript
# WRONG
$CollisionShape3D.scale = Vector3(2, 1, 2)

# RIGHT
var shape := $CollisionShape3D.shape.duplicate() as BoxShape3D
shape.size = Vector3(2.0, 1.0, 2.0)
$CollisionShape3D.shape = shape
```

Uniform scale sometimes works but has caveats. Non-uniform scale is always broken.

---

## CharacterBody Specific Issues

| Problem | Cause | Fix |
|---------|-------|-----|
| Stuck on walls | `wall_min_slide_angle` too low | Raise to 15-30° |
| Slides down slopes when idle | `floor_stop_on_slope` is false | Set to true |
| Bounces on floor | `floor_snap_length` too small | Increase (try 0.25 for 3D, 6-8 for 2D) |
| Not detecting floor | `safe_margin` too small or `floor_snap_length` zero | Tune both |
| Vibrating on moving platforms | `platform_floor_layers` not set | Set to platform's layer |
| Slow uphill, fast downhill | Normal slope behavior | Enable `floor_constant_speed` |
| Detaches from floor on bumps | Snap not engaging | Check `floor_snap_length`, add `apply_floor_snap()` |

---

## RigidBody Specific Issues

| Problem | Cause | Fix |
|---------|-------|-----|
| Objects fly away on spawn | Overlapping shapes at start | Pre-clear spawn points, spawn slightly above |
| Not sleeping when should | Oscillating near threshold | Add `linear_damp`, check restitution |
| Ignoring gravity | Wrong mode or freeze enabled | Check `freeze`, `custom_integrator` |
| Erratic behavior | Too many contacts | Set reasonable `max_contacts_reported` |
| No body_entered signals | `contact_monitor` false or `max_contacts_reported` = 0 | Enable both |
| Transform writes ignored | Setting transform outside `_integrate_forces` | Use forces/impulses or `_integrate_forces()` |

---

## Area2D/3D Issues

| Problem | Cause | Fix |
|---------|-------|-----|
| `body_entered` not firing | `monitoring` disabled, wrong layers, shape disabled | Check all three |
| Same body detected multiple times | Multiple shapes trigger multiple signals | Filter by body, not by signal count |
| `body_entered` vs `area_entered` confusion | Different signals for different object types | `body_entered` = PhysicsBody, `area_entered` = Area |
| Overlap not detected immediately | Lists update once per physics step | Use ShapeCast for same-frame detection |

---

## Performance Issues

### Too Many Active RigidBodies

- Enable sleeping (default)
- Freeze distant objects (`freeze = true`)
- Use simpler shapes for background clutter
- Consider PhysicsServer direct API for thousands of identical objects

### Too Many Collision Checks

- Simplify shapes (primitives over convex over concave)
- Reduce layers scanned (narrow masks)
- Avoid dense Area-vs-Area overlap graphs
- One collector Area instead of many individual Areas

### Physics Step Taking Too Long

- Reduce `physics_ticks_per_second` if acceptable
- Simplify scene (fewer active bodies, simpler shapes)
- Profile: watch `Performance.TIME_PHYSICS_PROCESS` and `PHYSICS_3D_COLLISION_PAIRS`

---

## Large World Coordinates

### Precision Ranges (single-precision 3D)

| Camera Style | Acceptable Range | Notes |
|-------------|-----------------|-------|
| First-person | ~4,096 units | Artifacts visible early |
| Third-person | ~8,192 units | |
| Top-down | ~16,384–32,768 units | |
| Beyond 32,768 | Double precision usually required | |

### Solutions

**Double Precision Build**: Compile with `precision=double`. Costs memory and CPU. Best for space games, planetary scale, ultra-fast traversal.

**Origin Shifting**: Keep active play space near origin, shift world periodically. More complex (especially multiplayer) but stays within single-precision performance.

### Symptoms of Precision Loss

- Physics bodies jitter or drift
- Collision detection becomes unreliable
- Objects "vibrate" when far from origin
- Joints stretch or explode at distance
