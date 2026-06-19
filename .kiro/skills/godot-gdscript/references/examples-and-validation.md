# Examples and Validation

Use this file before finalizing implementation guidance for a bounded task.

## Example Input

Task:

- add a `CharacterBody2D` dash ability
- targets are already approved
- acceptance criteria say dash must be physics-safe and interrupt normal movement briefly

## Good Output Shape

```md
- files or nodes changed:
  - `scripts/player/player_controller.gd`
- integration points inspected:
  - `scenes/player/Player.tscn`
  - input actions in `project.godot`
- MCP sync or inspection:
  - `search_symbols` if the existing movement entry points were unclear
  - `refresh_filesystem` after editing `scripts/player/player_controller.gd`
- callbacks:
  - `_physics_process(delta)` handles dash timing and velocity application
- signals or groups:
  - no new architecture unless already approved
- critical conventions:
  - typed variables
  - Godot 4 movement APIs
  - no old `move_and_slide(velocity)` pattern
- validation performed:
  - `get_diagnostics` reports no compiler errors for `scripts/player/player_controller.gd`
  - if no playtest happened, end with the exact runtime validation step still required
- pitfalls:
  - do not move dash timing to `_process()` if gameplay depends on physics behavior
```

## Validation Checklist

- guidance stays within the task
- actual project artifacts are named
- typed GDScript is used
- Godot 4 APIs and signal syntax are correct
- editor-connected work includes `refresh_filesystem` before LSP validation
- implementation does not silently add persistence, resource redesign, or architecture changes

## Bad Signs

- output turns into a general GDScript tutorial
- output only gives advice instead of concrete bounded implementation work
- architecture changes appear without escalation
- outdated Godot 3 idioms
- editor-connected output skips `refresh_filesystem` and still treats diagnostics as current

## Escalate

Escalate if:

- mutable resource behavior changes the task -> `godot-scene-resource`
- save/load or restoration becomes required -> `godot-persistence`
- task assumptions are no longer valid -> `godot-architect`
