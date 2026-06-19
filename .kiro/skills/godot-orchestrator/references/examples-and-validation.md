# Examples and Validation

Use this file before finalizing the ordered task list.

## Example Input

Approved architecture:

- composition-based arena scene
- immutable enemy tuning resources
- runtime enemy state on nodes
- GDScript implementation

## Good Output Shape

```md
## T1: Define Enemy Resource Boundaries
- goal: confirm what enemy state is shared vs per-instance
- targets:
  - scene/resource design note
- depends_on:
  - none
- preconditions:
  - architecture decision approved
- context_to_read:
  - scenes/enemy/Enemy.tscn
  - resources/enemy/EnemyConfig.tres
- mcp_context:
  - none
- mcp_tools:
  - none
- sync_steps:
  - none
- validation:
  - two enemy instances can be reasoned about without shared mutable state
- approval_requirements:
  - none
- blocked_if:
  - the current enemy scene/resource layout cannot be identified -> inspect project files before continuing
- skills_to_activate:
  - godot-scene-resource
- traces:
  - scoped brief: "enemy tuning data reused across instances"
- error_notes:
  - none

## T2: Implement Player Movement Slice
- goal: add bounded player movement and combat input in GDScript
- targets:
  - player controller script
  - scene wiring notes
- depends_on:
  - T1
- preconditions:
  - movement architecture approved
  - required input actions are known
- context_to_read:
  - scripts/player/player_controller.gd
  - scenes/player/Player.tscn
  - project.godot input map section
  - any existing movement helpers found by symbol search
- mcp_context:
  - none
- mcp_tools:
  - search_symbols for the player movement entry points if naming is unclear
  - refresh_filesystem for scripts/player/player_controller.gd after edits
  - get_diagnostics for scripts/player/player_controller.gd
- sync_steps:
  - refresh the edited script before trusting diagnostics
- validation:
  - get_diagnostics reports no errors for the edited script, and the task ends with the exact runtime check still required if no playtest was run
- approval_requirements:
  - none
- blocked_if:
  - input actions are missing or scene target is unknown -> re-open architecture or inspect project setup
- skills_to_activate:
  - godot-gdscript
- traces:
  - scoped brief: "one player class, one arena scene"
- error_notes:
  - none

## T3: Assemble Pause Menu Scene
- goal: add a bounded pause menu tree to the existing game scene
- targets:
  - scenes/main/Game.tscn
  - scenes/ui/PauseMenu.tscn or equivalent UI subtree
  - scripts/ui/pause_menu.gd
- depends_on:
  - T2
- preconditions:
  - UI host scene is approved
  - the task explicitly allows either editing an existing scene or creating a starter `.tscn` on disk first
- context_to_read:
  - scenes/main/Game.tscn
  - scripts/ui/pause_menu.gd
  - input actions: pause, ui_accept, ui_cancel
- mcp_context:
  - scene://current/tree after opening the target scene
- mcp_tools:
  - open_scene on scenes/main/Game.tscn
  - add_node for the UI root and child controls
  - update_property for names, mouse filters, focus, pause mode, and theme/script references
  - set_anchor_preset for full-rect and centered layout nodes
  - connect_signal for button actions if the task includes them
  - save_scene
- sync_steps:
  - refresh_filesystem for scripts/ui/pause_menu.gd if it was edited on disk before wiring it into the scene
- validation:
  - re-read scene://current/tree to confirm the expected UI hierarchy exists, then end with any runtime pause or navigation checks still required
- approval_requirements:
  - none unless the task explicitly removes existing nodes
- blocked_if:
  - the target scene cannot be opened, or a new scene file is required but the task does not permit creating a starter `.tscn` on disk first
- skills_to_activate:
  - godot-ui-core
- traces:
  - scoped brief: "minimal HUD"
- error_notes:
  - none

## T4*: Write property test for enemy instance isolation
- goal: verify that two enemy instances do not share mutable state
- targets:
  - test script
- depends_on:
  - T1
  - T2
- preconditions:
  - enemy resource boundaries are defined
  - enemy scene is implemented
- context_to_read:
  - scenes/enemy/Enemy.tscn
  - resources/enemy/EnemyConfig.tres
- mcp_context:
  - none
- mcp_tools:
  - none
- sync_steps:
  - none
- validation:
  - test passes: mutating one enemy instance does not affect another
- approval_requirements:
  - none
- blocked_if:
  - enemy implementation is not complete
- skills_to_activate:
  - godot-gdunit4
- traces:
  - architect property 1: "for any two enemy instances, mutating runtime state on one does not affect the other"
- error_notes:
  - none

## T5: Duplicate Enemy Spawn Points
- goal: clone existing spawn point nodes to populate the arena
- targets:
  - scenes/main/Arena.tscn
- depends_on:
  - T2
- preconditions:
  - at least one spawn point node exists in the arena scene
- context_to_read:
  - scenes/main/Arena.tscn
- mcp_context:
  - find_nodes with type "Marker3D" and group "spawn_points" to locate existing spawn points
- mcp_tools:
  - open_scene on scenes/main/Arena.tscn
  - find_nodes to locate existing spawn point nodes by type or group
  - duplicate_node to clone each spawn point
  - update_property to set new positions on the duplicates
  - save_scene
- sync_steps:
  - none (all work is MCP scene mutation)
- validation:
  - re-read scene://current/tree or find_nodes to confirm the expected spawn point count
- approval_requirements:
  - none
- blocked_if:
  - no existing spawn point node to clone -> create one with add_node first
- skills_to_activate:
  - godot-blockout-3d
- traces:
  - scoped brief: "arena with multiple spawn points"
- error_notes:
  - none

## T6*: Visual Verification of Arena Layout
- goal: capture a screenshot of the running arena to verify spawn point placement
- targets:
  - scenes/main/Arena.tscn
- depends_on:
  - T5
- preconditions:
  - arena scene is playable
  - game can be launched with run_scene or F5
- context_to_read:
  - scenes/main/Arena.tscn
- mcp_context:
  - none
- mcp_tools:
  - run_scene on scenes/main/Arena.tscn (or set_main_scene + run_scene)
  - editor_screenshot with max_resolution 512 to capture the game viewport
- sync_steps:
  - none
- validation:
  - screenshot shows spawn points distributed across the arena; visual inspection by the developer
- approval_requirements:
  - none
- blocked_if:
  - game cannot launch (compile errors, missing resources) -> fix diagnostics first
- skills_to_activate:
  - godot-blockout-3d
- traces:
  - none
- error_notes:
  - none

## T7: Capture Lessons if Assumptions Break
- goal: record reusable failure if implementation exposes a broken planning assumption
- targets:
  - retro note
- depends_on:
  - T2
  - T3
- preconditions:
  - a reusable failure or wrong assumption was actually observed
- context_to_read:
  - task output and concrete evidence of failure
- mcp_context:
  - none
- mcp_tools:
  - none
- sync_steps:
  - none
- validation:
  - target skill to improve is named
- approval_requirements:
  - none
- blocked_if:
  - root cause is still unknown -> continue debugging before writing retro
- skills_to_activate:
  - godot-retro
- traces:
  - none
- error_notes:
  - none
```

## Validation Checklist

- each task has one owner skill
- each task has one validation path
- dependencies are explicit
- first inspection targets are explicit
- MCP-backed tasks name their resource reads, tool order, sync steps, and approval gates
- task list does not hide architecture decisions
- task list does not collapse multiple subsystems into one task
- optional tasks are marked with `*` and are not on the critical path
- when architect provides correctness properties, each has a test task
- when architect classifies errors, the classification appears in relevant task error_notes
- traces link tasks back to source requirements when available

## Bad Signs

- "build combat, save/load, and UI"
- no `depends_on`
- no `context_to_read` or blocked-state definition
- generic skill routing like "godot"
- all tasks marked as optional (nothing is on the critical path)
- error handling tasks that add silent recovery without architect approval

## Escalate

Re-open architecture instead of patching the task list when:

- a task requires a new core pattern
- resource ownership changes invalidate earlier assumptions
