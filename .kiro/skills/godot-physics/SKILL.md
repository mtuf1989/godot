---
name: godot-physics
description: |
  Implement and configure Godot 4.4+ built-in physics systems (2D and 3D) — body selection,
  collision setup, raycasting, space queries, joints, physics interpolation, Jolt vs Godot
  Physics backend decisions, common gameplay patterns, and troubleshooting.
  Use when the user needs to set up CharacterBody2D/3D movement, configure move_and_slide
  parameters, implement RigidBody dynamics, set up collision layers and masks, create
  raycasts or shape casts, build hitbox/hurtbox systems, configure joints or ragdolls,
  enable physics interpolation, choose between Jolt and Godot Physics, implement platformer
  movement (coyote time, jump buffering, wall jump), top-down movement, projectile systems,
  moving platforms, stair stepping, or troubleshoot physics issues (tunneling, jitter,
  tile-edge bumps, stacking instability, scaled shapes).
  Also use when someone mentions CharacterBody2D, CharacterBody3D, RigidBody2D, RigidBody3D,
  StaticBody, AnimatableBody, move_and_slide, move_and_collide, is_on_floor, floor_snap_length,
  safe_margin, collision_layer, collision_mask, PhysicsDirectSpaceState, intersect_ray,
  intersect_shape, cast_motion, ShapeCast2D, ShapeCast3D, RayCast2D, RayCast3D,
  PinJoint, HingeJoint, Generic6DOFJoint, ConeTwistJoint, SliderJoint, DampedSpringJoint,
  PhysicalBoneSimulator3D, ragdoll, physics_interpolation, reset_physics_interpolation,
  physics_ticks_per_second, Jolt Physics, continuous_cd, CCD, SeparationRayShape,
  one_way_collision, platform_floor_layers, Area2D, Area3D, body_entered, PhysicsMaterial,
  apply_force, apply_impulse, _integrate_forces, PhysicsServer2D, PhysicsServer3D,
  or asks to "add physics," "make the character move," "set up collision," "fix tunneling,"
  "add a joint," "set up ragdoll," "fix jitter," "add coyote time," "implement wall jump,"
  "push rigid bodies," "set up moving platforms," "fix stacking," or "choose physics backend."
  Even if the user just says "the character falls through the floor" or "physics feels wrong"
  or "how should I set up collision layers" in a Godot project, this skill applies.
  Do NOT use for GPU compute physics simulation (boids, SPH, spatial hashing) — use godot-compute.
  Do NOT use for navigation/pathfinding — that's a separate system.
  Do NOT use for animation-driven root motion or procedural animation — use godot-animation.
  Do NOT use for Tween-based VFX or visual juice — use godot-vfx-2d.
---

# Godot Physics

Implement physics systems in Godot 4.4+ using correct body selection, collision architecture, query patterns, interpolation, and backend-aware tuning for both 2D and 3D.

Read `../../foundation/Godot Nuanced Development Practices.md` when implementation details are ambiguous.

## Responsibility

- Select the correct body type for a gameplay requirement.
- Configure `move_and_slide()` parameters for robust character controllers.
- Design collision layer/mask matrices that scale.
- Implement raycasting, shape casting, and direct space state queries.
- Set up joints, constraints, and ragdoll systems.
- Configure physics interpolation for smooth rendering.
- Choose and configure Jolt vs Godot Physics backends.
- Implement common gameplay patterns (platformer, top-down, projectiles, hitbox/hurtbox).
- Diagnose and fix physics problems (tunneling, jitter, tile bumps, instability).

## Do Not Use When

- GPU-driven physics simulation (boids, SPH, compute shaders) → `godot-compute`
- Navigation and pathfinding → separate system
- Animation-driven root motion, procedural animation → `godot-animation`
- Architecture decisions unrelated to physics → `godot-architect`
- General GDScript without physics context → `godot-gdscript`
- Camera rigs, follow modules, shake → `godot-camera`
- Structured logging instrumentation → `godot-logger`

## Use When

- Setting up CharacterBody2D/3D movement controllers
- Configuring `move_and_slide()` parameters or debugging floor/wall detection
- Implementing RigidBody dynamics, forces, impulses, or `_integrate_forces()`
- Designing collision layer/mask matrices
- Building raycasts, shape casts, or direct space state queries
- Setting up hitbox/hurtbox systems with Area2D/3D
- Configuring joints, constraints, or ragdoll systems
- Enabling and debugging physics interpolation
- Choosing between Jolt and Godot Physics backends
- Implementing platformer mechanics (coyote time, jump buffer, wall jump, slopes)
- Implementing top-down movement, projectiles, or moving platforms
- Troubleshooting tunneling, jitter, tile-edge bumps, stacking instability
- Using PhysicsServer direct API for high-entity-count scenarios

## Decision Framework

### Body Type Selection

| Need | Body | Why |
|------|------|-----|
| Player/NPC with authored movement | `CharacterBody2D/3D` | Direct velocity control, floor/wall/ceiling detection, platform support |
| Simulation-driven object (crates, debris, ragdoll) | `RigidBody2D/3D` | Physics engine handles forces, collisions, sleeping |
| Immovable world geometry | `StaticBody2D/3D` | Zero cost, conveyor via `constant_linear_velocity` |
| Moving platform / animated mover | `AnimatableBody2D/3D` | Estimates velocity, carries riders, `sync_to_physics` for AnimationPlayer |
| Trigger volume / gravity zone | `Area2D/3D` | Overlap detection, gravity/damping override, no collision response |

### move_and_slide vs move_and_collide

- **`move_and_slide()`**: Use for characters. Handles floor/wall/ceiling classification, slope behavior, platform carry, multi-iteration sliding. Mutates `velocity`.
- **`move_and_collide()`**: Use for custom response (ricochet, bounce, one-shot sweep). Returns `KinematicCollision`, leaves response to you.

### Jolt vs Godot Physics (3D only)

**Default to Jolt** for 3D projects in 4.4+. It is the default for new projects in 4.6.

| Prefer Jolt | Prefer Godot Physics |
|-------------|---------------------|
| Ragdolls, dense stacks, complex joints | Simple 3D, already tuned to legacy |
| CylinderShape3D needed | Memory-constrained platforms |
| VehicleBody3D (still limited but better) | 2D-only project (Jolt is 3D-only) |
| Concave environment traversal | Joint soft-limit properties critical |
| World-anchored constraints | — |

Switch: `Project Settings > Physics > 3D > Physics Engine`. Pin explicitly; don't rely on `DEFAULT`.

### Physics Interpolation

Enable: `Project Settings > Physics > Common > Physics Interpolation`

Rules:
- All authoritative movement in `_physics_process()`, never `_process()`
- Call `reset_physics_interpolation()` after teleports, spawns, scene transitions
- Camera: set `top_level = true`, `physics_interpolation_mode = OFF`, follow target's `get_global_transform_interpolated()` in `_process()`
- Test at 10 TPS to make mistakes obvious

### Collision Layer Strategy

Layers = "I am on this channel." Masks = "I scan for these channels."

Reserve dedicated layers for: world solid, one-way platforms, player body, enemy body, player hurtbox, enemy hurtbox, player hitbox, enemy hitbox, pickups, triggers, AI sensors, moving platforms.

Key rules:
- One semantic meaning per layer
- Projectiles never scan projectile layers unless gameplay requires it
- Prefer one-sided detection (player collector Area scans pickups, not 500 pickup Areas scanning player)
- Runtime mask changes: compute final integer and assign once, don't call `set_collision_mask_value()` per-bit

### Shape Performance Hierarchy

**Fastest → Slowest:**
- 2D: Circle > Rectangle > Capsule > ConvexPolygon > ConcavePolygon
- 3D: Sphere > Box > Capsule > Cylinder > ConvexPolygon > HeightMap > ConcavePolygon

Rules:
- ConcavePolygon: static-only, never on dynamic/kinematic bodies
- CylinderShape3D: documented bugs, prefer Capsule or Box
- Never scale collision shapes via node transform — resize the shape resource
- SeparationRayShape: stair/ledge bias tool, not general collision

## Key Parameters Quick Reference

### CharacterBody Critical Properties

| Property | Default (3D) | Purpose |
|----------|-------------|---------|
| `floor_max_angle` | 45° | Max slope angle that counts as floor |
| `floor_snap_length` | 0.1 | Downward snap to maintain floor contact |
| `safe_margin` | 0.001 (3D), 0.08 (2D) | Recovery margin for depenetration |
| `max_slides` | 4 | Iteration budget per move_and_slide call |
| `floor_stop_on_slope` | true | Prevents idle sliding on slopes |
| `floor_constant_speed` | false | Maintains speed on slopes |
| `platform_floor_layers` | all | Which layers count as rideable platforms |
| `platform_on_leave` | ADD_VELOCITY | What happens when leaving a platform |

### RigidBody Key Settings

| Setting | Purpose |
|---------|---------|
| `continuous_cd` | Enable CCD for fast movers (2D: CAST_RAY or CAST_SHAPE; 3D: boolean) |
| `contact_monitor` + `max_contacts_reported` | Required for body_entered/exited signals |
| `freeze` + `freeze_mode` | STATIC (no collision response) vs KINEMATIC (still collides) |
| `custom_integrator` | Disables default gravity/damping, you own `_integrate_forces()` |
| `lock_rotation` | Forces only apply linear movement |
| `can_sleep` | Allow body to sleep when at rest |

### Jolt 3D Recommended Defaults (4.6)

| Setting | Value | Notes |
|---------|-------|-------|
| `velocity_steps` | 10 | Raise first for friction/stack issues |
| `position_steps` | 2 | Raise only for positional drift |
| `baumgarte_stabilization_factor` | 0.2 | Position-only correction (no overshoot) |
| `collision_margin_fraction` | 0.08 | Shrink-and-shell convex radius |
| `max_bodies` | 10240 | Raise if hitting limits |
| `temporary_memory_buffer_size` | 32 MiB | Stack allocator |

## Workflow

1. Confirm operating mode (editor-connected vs filesystem-only) per foundation §3.
2. Identify the physics task category and read the relevant reference file.
3. Inspect the project's physics backend (`project.godot` → `physics/3d/physics_engine`) and existing collision layer naming.
4. Implement using patterns from the appropriate reference. Use typed GDScript, `_physics_process()` for all authoritative movement.
5. Add GLog instrumentation for physics events that matter to gameplay narrative (state transitions, collision events, teleports). See foundation §6.5.
6. Validate: use MCP Server to `get_diagnostics` on changed scripts, `run_scene` if runtime behavior matters.
7. If physics feels wrong at runtime, consult `references/troubleshooting.md`.
8. Escalate to `godot-retro` if a reusable process failure was discovered.

## Output

- Files changed (scripts, project settings)
- Body types and collision layers chosen with rationale
- Physics backend confirmed
- Validation performed and its limits
- GLog calls added for physics-relevant gameplay events
- Exact blocker and next action if blocked

## Reference Files

Read the specific reference needed for the task:

| Task | Reference |
|------|-----------|
| CharacterBody movement, RigidBody dynamics, body selection | `references/body-types-and-movement.md` |
| Collision layers, masks, shapes, Areas, one-way platforms | `references/collision-and-shapes.md` |
| Raycasting, ShapeCast, direct space state queries | `references/queries-and-raycasting.md` |
| Jolt vs Godot Physics comparison and migration | `references/jolt-vs-godot-physics.md` |
| Joints, constraints, ragdolls, stability | `references/joints-and-ragdolls.md` |
| Physics interpolation, tick rate, camera follow | `references/interpolation-and-timing.md` |
| Platformer, top-down, projectiles, hitbox/hurtbox, platforms | `references/gameplay-patterns.md` |
| Tunneling, jitter, tile bumps, stacking, scaling, large worlds | `references/troubleshooting.md` |
| PhysicsServer direct API, RID management, bullet pools | `references/server-direct-api.md` |

## Quality Gates

- Body type matches the gameplay requirement, not habit.
- Collision layers have clear semantic meaning; no "scan everything" masks.
- All movement code lives in `_physics_process()`.
- `reset_physics_interpolation()` is called after every teleport/spawn.
- Shapes are never scaled via node transform.
- ConcavePolygon is never used on dynamic bodies.
- Jolt-specific settings are only applied when Jolt is the active backend.
- `safe_margin` and `floor_snap_length` are tuned to the project's scale.
- Code uses typed GDScript throughout.

## Failure Modes

- Do NOT set `global_transform` on RigidBody every frame — use `_integrate_forces()` or forces/impulses.
- Do NOT use `Timer` nodes for feel-critical windows (coyote, jump buffer) — use float countdowns in `_physics_process()`.
- Do NOT assume `Area2D/3D` overlap lists update immediately — they update once per physics step.
- Do NOT copy Jolt solver settings to Godot Physics or vice versa — they are structurally different solvers.
- Do NOT use `is_on_floor()` before calling `move_and_slide()` — it reports state from the last call.
- Do NOT disable sleeping globally — it's essential for performance with many rigid bodies.
- Do NOT expect `CharacterBody` to be affected by `PhysicsMaterial` friction/bounce — it's script-driven.
- Do NOT query `PhysicsDirectSpaceState` outside `_physics_process()` — the space may be locked.
- Do NOT claim validation that was not performed.
- Do NOT widen scope silently — escalate to `godot-architect` if physics decisions imply architectural changes.

## References

Read only as needed:

- `references/body-types-and-movement.md`
- `references/collision-and-shapes.md`
- `references/queries-and-raycasting.md`
- `references/jolt-vs-godot-physics.md`
- `references/joints-and-ragdolls.md`
- `references/interpolation-and-timing.md`
- `references/gameplay-patterns.md`
- `references/troubleshooting.md`
- `references/server-direct-api.md`
- `../../foundation/Godot Nuanced Development Practices.md`
