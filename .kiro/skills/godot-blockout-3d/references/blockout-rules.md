# Blockout Rules

Use this file before building or reviewing any 3D prototype space.

## Scale and Metrics

- Treat Godot 3D space as metric: `1 unit = 1 metre`.
- Express blockout sizes in metres and keep that visible in node names or notes when it helps review.
- Define one blockout scale profile per project. At minimum, confirm or note:
  - player footprint or collision radius
  - standing height
  - step or climb limit
  - minimum door clearance
  - minimum corridor width
  - interaction reach
- If the project has no chosen profile yet, use this as a sanity baseline and state it explicitly:
  - `cell_size = 0.25`
  - `cell_height = 0.25`
  - `agent_radius = 0.5`
  - `agent_height = 1.5`
  - `agent_max_climb = 0.25`
  - `agent_max_slope = 45`

## Grid and Snap

- Use the 1 m viewport grid for macro layout.
- Use a translation snap of `0.25 m` or `0.5 m` for actual placement unless grounded project facts require another increment.
- Use rotation snapping for anything angled. Prefer repeatable angles over freehand rotations.
- Keep most blockout geometry axis-aligned. Reserve angled pieces for deliberate traversal features.
- Never close gaps with arbitrary decimal nudges. Change the module size or snap increment instead.
- Do not rely on `CSGShape3D.snap`; it is not a working alignment tool.

## Primitive-First Build Rules

- Default to `MeshInstance3D` plus primitive meshes for most blockout geometry.
- Prefer these building blocks:
  - floors and ceilings: `PlaneMesh` or a thin `BoxMesh`
  - walls: `BoxMesh` segments in repeatable lengths
  - pillars: `CylinderMesh` or `BoxMesh`
  - obstacles and cover: `BoxMesh` variants with readable heights
  - platforms: `BoxMesh` with enough thickness for stable collision
- Use larger, fewer pieces when possible. Avoid micro geometry that increases collider count and hides the main path.
- Keep a consistent material language:
  - floor
  - wall
  - blocker
  - hazard
  - interactable

## CSG Use Rules

- Use CSG only for local boolean-first tasks:
  - doorway or window cutouts
  - simple interior shells
  - quick subtractive stairs or ramps
- Keep CSG trees local and simple. Use `CSGCombiner3D` to limit boolean scope when needed.
- Keep furniture, props, and other unrelated shapes out of the same CSG tree.
- If a blockout can be expressed cleanly with primitive meshes, do not move it into CSG.

## Collision Rules

- Pair solid blockout geometry with `StaticBody3D` plus primitive `CollisionShape3D` wherever practical.
- Keep `CollisionShape3D` as a direct child of the owning `PhysicsBody3D`.
- Avoid non-uniform scale on collision objects. Resize the shape resource instead.
- Prefer primitive collision shapes for anything the player or dynamic bodies must touch reliably.
- Treat concave or trimesh collision as static-only fallback for level geometry, not as the default.
- Keep platforms, stairs, and blockers thick enough that collision remains stable.

## Navigation Placeholder Rules

- Only introduce nav placeholders when the task or project already depends on navigation.
- Treat the navmesh as the pathability source of truth. If collision says one thing and nav says another, fix the mismatch.
- Validate corridor widths, doors, ramps, and stairs against the scale profile and nav agent dimensions.
- Prefer physics shapes as the bake source when runtime or frequent rebakes matter.
- Avoid using region scaling as a shortcut for navmesh resizing. Rebuild or rebake instead.
- If the project has no navigation yet, note that nav validation was skipped rather than inventing it.

## Interaction and Tagging Rules

- Keep interaction hooks separate from solid geometry.
- Use `Area3D` plus simple convex shapes for:
  - pickups
  - trigger zones
  - checkpoints
  - hazard volumes
- Label or group prototype affordances clearly so validation is fast. If the project has no convention yet, default to:
  - `blockout_interactive`
  - `blockout_hazard`
  - `blockout_nav_source`
- If a trigger shape becomes complicated, split it into smaller volumes instead of making one complex region.

## Pattern Shortcuts

### Room

- Start with a floor, four wall segments, and only the exits required by the task.
- Use a simple primitive gap before reaching for subtractive CSG.
- Prioritize spawn, exits, sightlines, and one or two traversal changes.

### Corridor

- Build corridors from repeatable wall modules aligned to snap increments.
- Keep width and height consistent with the scale profile.
- Prefer clean orthogonal corners unless an angled transition is itself the feature under test.

### Arena

- Use one or a few large floor pieces.
- Add a readable boundary and a few landmark obstacles.
- Avoid tiny clutter that makes the path ambiguous.

### Stairs or Ramp

- Prefer stepped `BoxMesh` stairs for easy inspection and tuning.
- Use a ramp only when slope behavior is the thing being tested.
- Validate elevation changes against movement and nav limits, not just visual appearance.
