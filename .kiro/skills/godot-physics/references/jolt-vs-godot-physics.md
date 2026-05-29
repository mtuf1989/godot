# Jolt vs Godot Physics

## Status Across Versions

| Version | Jolt Status |
|---------|-------------|
| 4.4 | Built-in alternative, experimental, opt-in |
| 4.5 | Stabilization, ~20 fixes, still optional |
| 4.6 | Default for new 3D projects, experimental label removed |

**Scope**: Jolt is 3D-only. Does not affect 2D physics at all.

**Activation**: `Project Settings > Physics > 3D > Physics Engine`. Pin explicitly — don't rely on `DEFAULT` token.

**Thread safety**: Supports `Run On Separate Thread` but documented as "not thoroughly tested." Degrades error-context readability.

---

## Key Behavioral Differences

### Baumgarte Stabilization

- **Godot Physics**: Applies correction "like a spring" — can accelerate bodies, overshoot, and separate too far
- **Jolt**: Applies correction to position only, not velocity — cannot overshoot, but deep penetrations take longer to settle

**Practical impact**: Jolt feels more damped and less explosive under initial overlap. Godot Physics can feel "bouncy" when jointed bodies start badly overlapped.

### Collision Margins

- **Godot Physics**: Largely ignores `Shape3D.margin` values
- **Jolt**: Actively uses shrink-and-shell model. Governed by `physics/jolt_physics_3d/collisions/collision_margin_fraction` (default 0.08). Can alter contact normals on small convex shapes.

### Ghost Collisions / Internal Edges

- **Jolt**: Active edge detection + enhanced internal edge removal. Works best within a single body/compound shape. Does NOT apply between separate bodies.
- **Godot Physics**: No equivalent mitigation

**Production rule**: Merge adjacent floor pieces into one static body so Jolt can treat seams as internal.

### Area3D Behavior

- **Jolt**: `Area3D` does NOT detect `StaticBody3D` overlaps by default (performance optimization)
- **Godot Physics**: Detects them by default
- **Fix**: Enable `Physics > Jolt Physics 3D > Simulation > Areas Detect Static Bodies`

### Contact Impulses

- **Jolt**: `get_contact_impulse()` is an estimate, most accurate when two bodies aren't simultaneously touching many others
- **Godot Physics**: Not reliably available

### Ray Face Index

- **Jolt**: Returns `face_index = -1` by default. Enable `Physics > Jolt Physics 3D > Queries > Enable Ray Cast Face Index` for triangle indices (~25% memory increase on concave shapes)
- **Godot Physics**: Returns face index normally

### Single-Body Joints

- **Jolt**: Supports world-anchored constraints (one body attached to "the world")
- **Godot Physics**: Requires two bodies. Legacy treats omitted body differently.
- **Compatibility switch**: Available for legacy behavior

### Unsupported Joint Properties (Jolt)

Several legacy 3D joint properties are NOT supported under Jolt:
- `bias`, `softness`, `relaxation` on various joints
- Restitution, damping, ERP on some joint types
- `PARAM_LIMIT_SOFTNESS` on HingeJoint3D (deprecated, "never used by engine")

**Design rule**: Don't depend on these values for structural behavior. Treat them as optional polish.

---

## Performance Comparison

| Scenario | Jolt | Godot Physics |
|----------|------|---------------|
| Stacked objects | Stable (warm-started solver) | Wobbles without high iterations |
| Ragdolls | Stable, separate velocity/position phases | Can overshoot on deep penetration |
| CylinderShape3D | Improved (still least stable primitive) | Documented bugs |
| VehicleBody3D | Better environment, still limited node | Same limitations |
| Large body counts | Jobified, designed for thousands | Single-threaded solver |
| Memory | 32 MiB temp allocator + per-body | Generally lower baseline |

### Jolt Limits (defaults)

- `max_bodies`: 10,240
- `max_body_pairs`: 65,536
- `max_contact_constraints`: 20,480
- `temporary_memory_buffer_size`: 32 MiB

Raise these before concluding the backend is unstable at high density.

---

## Migration Strategy

There is NO 1:1 numeric conversion from `solver_iterations` to Jolt's `(velocity_steps, position_steps)`. The solvers are structurally different.

### Procedural Migration Order

1. Pin physics tick rate
2. Switch to Jolt defaults: `velocity_steps = 10`, `position_steps = 2`
3. Raise **velocity steps first** if friction, joint drive, or stack settling looks soft
4. Raise **position steps** only if visible positional drift remains
5. Retune Baumgarte only if depenetration timing feels off
6. Retune damping (behavioral, not copy-paste values)
7. Retune friction last (effectiveness depends on velocity iterations)

### Common Migration Traps

- `Area3D` triggers stop working → enable Areas Detect Static Bodies
- Joint feel changes → legacy soft-limit properties are unsupported
- Small convex shapes feel different → collision margin fraction affects normals
- Kinematic frozen bodies don't report contacts → enable Generate All Kinematic Contacts

---

## Recommended 4.6 Starting Configuration

```
physics/3d/physics_engine = "Jolt Physics"  # Pin explicitly

# Solver
jolt_physics_3d/simulation/velocity_steps = 10
jolt_physics_3d/simulation/position_steps = 2
jolt_physics_3d/simulation/baumgarte_stabilization_factor = 0.2

# Collision
jolt_physics_3d/collisions/collision_margin_fraction = 0.08
jolt_physics_3d/collisions/active_edge_threshold = 50°  (approx)

# Keep enabled
jolt_physics_3d/collisions/use_enhanced_internal_edge_removal = true

# Keep disabled unless needed
jolt_physics_3d/simulation/generate_all_kinematic_contacts = false
jolt_physics_3d/queries/enable_ray_cast_face_index = false
jolt_physics_3d/simulation/run_on_separate_thread = false  # Until stable
```

### When to Raise Settings

| Symptom | First Knob |
|---------|-----------|
| Sliding friction too weak | Raise `velocity_steps` |
| Stacks still wobble | Raise `velocity_steps`, then `position_steps` |
| Deep penetration lingers | Raise `baumgarte_stabilization_factor` slightly |
| Small shapes feel "rounded" | Lower `collision_margin_fraction` (test carefully) |
| Ragdoll joints stretch | Raise `velocity_steps` + check mass ratios |

---

## Decision Matrix

**Choose Jolt when:**
- Ragdoll chains, motorized constraints, world-anchored joints
- Large high-mass-ratio stacks
- Frequent kinematic traversal over concave environments
- Robust soft-body collisions needed
- CylinderShape3D required and capsule won't work
- New 3D project (it's the default)

**Keep Godot Physics when:**
- 3D layer is simple and already tuned
- Project depends on legacy joint soft-limit properties
- Memory-constrained platform
- Migration cost exceeds physics upside
- Predominantly 2D project (Jolt doesn't help 2D)

---

## SoftBody3D

- More robust in Jolt than Godot Physics
- 4.5 added force/impulse support and interpolation
- **Limitation**: `Area3D` and `SoftBody3D` do NOT interact under Jolt (no wind/gravity zones on soft bodies)
- For cloth/capes needing ambient forces: use bespoke code or alternative design
