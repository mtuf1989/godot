---
name: godot-blockout-3d
description: |
  Create prototype-only 3D blockouts for Godot using primitive meshes, scoped CSG, and placeholder collision or navigation conventions.
  Use when early 3D gameplay needs rooms, corridors, arenas, obstacles, stairs, platforms, trigger volumes, or any greybox/whitebox level geometry with honest validation and clear handoff before final environment art.
  Also use when someone says "block out a level," "prototype 3D space," "greybox," "whitebox," "CSG layout," "placeholder environment," or needs cheap 3D geometry to test traversal, collision, or spatial gameplay.
  If the task involves creating or editing early 3D spaces for gameplay iteration rather than final art, this skill should fire.
---

# Godot Blockout 3D

Create early 3D spaces that unblock gameplay and traversal work without inventing a production environment art pipeline.

Read `references/blockout-rules.md` first. Read `references/csg-limitations.md` before adding or expanding CSG usage. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the actual project surfaces that the blockout touches before editing:
  - target scenes and nearby integration points
  - player or controller collider dimensions, or the exact missing assumptions
  - physics layer and mask conventions for solids and triggers
  - whether navigation is present and which agent settings matter
  - existing scene, folder, and naming conventions for prototype spaces
- Create or update bounded prototype 3D layout using primitive meshes first and CSG only where boolean speed clearly helps.
- Encode traversal intent with collision, trigger volumes, and optional navigation placeholders.
- When the task touches editor-owned scene structure, use MCP scene tools for scene assembly and direct file edits only for starter `.tscn` files or supporting resources that must exist before opening the scene.
- Keep the work firmly in prototype territory:
  - readable spatial layout
  - collision sanity
  - interaction hooks as simple volumes
  - explicit production handoff when CSG or placeholders stop being the right tool
- Optionally add a minimal validation harness only when the task explicitly allows it.

## Use When

- Gameplay work is blocked on missing 3D space or placeholder environment geometry.
- The project needs cheap rooms, corridors, arenas, stairs, obstacles, blockers, or interaction volumes for iteration.
- The team needs consistent scale, snap, collision, and nav placeholder conventions for early 3D work.
- The task is still bounded enough to stay prototype-only.

## Do Not Use When

- The request is really about final environment art, UV authoring, material authoring, lighting polish, or production optimization.
- The task requires a broad level design pass instead of bounded geometry work in a concrete scene.
- The main problem is scene architecture, resource ownership, or save/load identity rather than blockout construction.
- The task depends on a full 3D controller or movement design that has not been scoped separately.

## Workflow

1. Read `references/blockout-rules.md` and `references/csg-limitations.md`.
2. Inspect the real targets before choosing conventions:
   - scenes to edit or create
   - player footprint, standing height, and traversal assumptions
   - solid, trigger, and navigation conventions already in the project
   - existing prototype materials or blockout scenes that must stay consistent
3. If a brand-new scene is required, create a starter `.tscn` on disk first, then call `refresh_filesystem` and `open_scene` before structural scene edits. If the task does not permit creating that starter file, treat the new-scene step as blocked.
4. Define or confirm one blockout scale profile for the task. If the project has none, use the defaults in `references/blockout-rules.md` as a sanity baseline and state that assumption explicitly.
5. When the task touches editor-owned scene structure, use MCP in this order:
   - `open_scene` for the target blockout scene
   - `scene://current/tree` to inspect the existing hierarchy before mutating it, or `find_nodes` for targeted lookups (e.g., find all `CollisionShape3D` or `Area3D` nodes)
   - `add_node` for `Node3D`, `MeshInstance3D`, `StaticBody3D`, `CollisionShape3D`, and `Area3D` structure
   - `duplicate_node` to clone existing geometry modules (wall segments, room templates, trigger volumes) instead of rebuilding them
   - `update_property` for transforms, layer or mask settings, and primitive properties (visibility, collision flags, numeric values)
   - `move_node` when reparenting is required
6. Build the smallest coherent layout that satisfies the task:
   - default to `MeshInstance3D` plus primitive meshes for most geometry
   - pair solid geometry with `StaticBody3D` plus primitive `CollisionShape3D`
   - use `Area3D` plus simple shapes for triggers, hazards, and interaction hooks
   - use CSG only for local boolean needs such as doorway cutouts or quick room shells
7. Keep the blockout readable under flat materials and default lighting. Walkable, blocked, hazardous, and interactive volumes must stay distinguishable without art polish. Edit supporting `.tres` resources or starter scene files directly on disk when that is more efficient, then call `refresh_filesystem` before relying on editor state.
8. If scene mutation happened through MCP, call `save_scene` after the layout is complete.
9. Read `references/examples-and-validation.md`, then validate with the strongest honest method available:
   - in editor-connected mode, re-read `scene://current/tree` to confirm the expected hierarchy before stronger editor or playtest checks
   - `editor_screenshot` after `run_scene` to visually verify blockout layout, scale, and spatial relationships in the running game
   - scale and snap consistency
   - collision sanity
   - optional nav placeholder sanity
   - room, corridor, or elevation traversal checks that match the requested slice
10. Escalate instead of improvising:
   - broad scope or level-planning ambiguity -> `godot-scope`
   - unresolved architectural tradeoffs -> `godot-architect`
   - risky scene or resource ownership questions -> `godot-scene-resource`
   - reusable skill failure or weak workflow -> `godot-retro`

## Output

Leave behind bounded prototype blockout work with:

- files, scenes, or resources created or changed
- MCP resources, tools, and sync steps used when editor-connected
- geometry patterns covered
- scale profile, collision, and nav assumptions used
- whether primitives, CSG, or both were used
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The work stays in metre-based blockout scale and uses explicit snap rules.
- Editor-connected scene mutation uses MCP scene tools and scene-tree reads instead of blind edits to an existing `.tscn`.
- Most geometry uses primitive meshes and primitive collision unless a local CSG boolean is clearly justified.
- Traversal intent is enforced by collision or nav, not only by visuals.
- Triggers and hazards are simple, clearly labeled volumes.
- CSG use stays within the limits in `references/csg-limitations.md`.
- Validation claims match the real operating mode.

## Failure Modes

- Do not let prototype blockout drift into final art, UV work, shader polish, or lighting polish.
- Do not invent player scale, navigation settings, or layer conventions when the project can be inspected.
- Do not rely on concave or CSG-derived collision for detailed gameplay feel when simpler shapes are required.
- Do not widen the task into controller design, combat design, or broad level direction.
- Do not edit an existing blockout scene blindly on disk when the task expects editor-owned scene mutation through MCP.
- Do not claim editor or runtime validation when only filesystem inspection was performed.

## References

Read only as needed:

- `references/blockout-rules.md`
- `references/csg-limitations.md`
- `references/examples-and-validation.md`
