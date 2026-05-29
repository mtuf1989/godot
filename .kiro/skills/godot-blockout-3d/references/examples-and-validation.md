# Examples and Validation

Use this file before finalizing 3D blockout work.

## Example Input

Task:

- build a prototype 3D space for traversal testing
- cover one room, one corridor, one arena pocket, and one simple trigger
- keep the layout cheap and easy to iterate
- do not begin a final environment art pass

## Good Output Shape

```md
- files changed:
  - res://scenes/blockout/test_room.tscn
  - res://scenes/blockout/materials/blockout_floor.tres
  - res://scenes/blockout/materials/blockout_hazard.tres
- project surfaces inspected:
  - project.godot
  - scenes/player/Player.tscn
  - scenes/navigation/NavTest.tscn
- MCP actions performed:
  - `open_scene` on `res://scenes/blockout/test_room.tscn`
  - `scene://current/tree` read before mutation
  - `add_node` for blockout geometry, collision, and trigger volumes
  - `update_property` for transforms, primitive mesh settings, collision shapes, and layers
  - `save_scene`
- conventions used:
  - metre-based scale with 0.25 m placement snap
  - primitive meshes for floors, walls, obstacles, and stairs
  - Area3D box trigger for the interaction volume
  - CSG limited to one doorway cutout
  - primitive collision for all player-contact surfaces
- validation performed:
  - `scene://current/tree` re-read confirms the expected geometry, collision, and trigger hierarchy after save
- limits:
  - editor traversal, collision debug, and nav bake still require stronger in-editor validation if they were not exercised
```

## Validation Ladder

- Editor-connected:
  - `open_scene` on the target blockout and read `scene://current/tree` before mutation
  - confirm the saved hierarchy, transforms, collision ownership, and trigger placement by re-reading `scene://current/tree`
  - inspect the blockout in the editor or playtest harness
  - confirm snap alignment, collision debug, trigger behavior, and traversal feel
  - if navigation is in scope, bake or inspect the navmesh and confirm the intended path is actually reachable
- Filesystem-only:
  - verify node structure, naming, collision ownership, and grounded scale assumptions
  - confirm primitives versus CSG usage follows the skill rules
  - end with the exact MCP or editor validation step still required
- Blocked:
  - stop if the target scene, player scale, or allowed destination for the blockout is unknown
  - stop if a new scene file is required but the task does not permit creating a starter `.tscn` before `open_scene`

## Checklist

- Actual consuming scenes, controllers, or project settings were inspected before choosing blockout conventions.
- Editor-connected scene work names the `scene://current/tree` read plus the MCP tool sequence used.
- The work stays in metre-based scale.
- Placement and rotation use explicit snap increments.
- Most geometry uses primitive meshes and primitive collision.
- Collision shapes are direct children of their owning physics bodies.
- No critical gameplay collision depends on non-uniform scaling.
- Triggers use simple `Area3D` volumes.
- If nav is in scope, the nav assumptions match the player or agent profile.
- Walkable, blocked, hazardous, and interactive spaces are distinct without relying on polished art.
- Validation claims match the real operating mode.

## Scenario Checks

### One Room

- The player can enter, turn, and exit without snagging on walls or obstacles.
- The main path is readable from the spawn point.
- The interaction volume is separate from solid geometry and easy to identify.

### Corridor to Arena

- Corridor width and ceiling height match the current scale profile.
- Arena obstacles create readable cover or route changes without collision seams.
- If nav is enabled, the intended path bakes cleanly through the corridor and arena.

### Stairs or Ramp

- Stairs use consistent riser and tread spacing, or the ramp uses one intentional slope.
- Traversal does not jitter or catch on tiny lips.
- If nav is enabled, the bake respects the expected climb or slope limits.

### Collision and Traversal Pass

- Perform a wall-hug pass around room boundaries and obstacles.
- Check that blocked areas stay blocked and intended routes stay open.
- If both collision and nav are present, they agree on what is traversable.

## Common Failures

- The layout reads only because of notes or art expectations, not because geometry is clear.
- A primitive-friendly shape was turned into CSG without a real boolean need.
- Collision is attached indirectly or relies on non-uniform scaling.
- Detailed CSG collision is treated as gameplay-ready.
- Nav settings were assumed without inspecting the project.
- The task edits an existing blockout scene on disk and never reconciles the editor-owned hierarchy through MCP.
- The final report claims editor validation that never happened.

## Escalate

- Scope is turning into broader level planning or environment direction -> `godot-scope`
- The task now depends on project-level structural decisions -> `godot-architect`
- Scene or resource boundaries are risky -> `godot-scene-resource`
- The blockout workflow itself needs reusable correction -> `godot-retro`
