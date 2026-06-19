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

---

## Example Input 2

Task:

- create a WaveDefinition resource script and a WaveManager that loads definitions by index
- targets: `scripts/data/wave_definition.gd`, `scripts/managers/wave_manager.gd`
- acceptance criteria: WaveManager exposes `start_wave(index)`, emits `wave_started` with the definition

## Good Output Shape 2

```md
- files or nodes changed:
  - `scripts/data/wave_definition.gd` (new Resource script)
  - `scripts/managers/wave_manager.gd` (new Node script)
- integration points inspected:
  - `data/waves/` directory for existing .tres files
  - parent scene where WaveManager will live
- MCP sync or inspection:
  - `refresh_filesystem` after each new .gd file
  - `get_diagnostics` on both scripts
- callbacks:
  - none (`start_wave` is called externally, not per-frame)
- signals or groups:
  - `signal wave_started(definition: WaveDefinition)`
- critical conventions:
  - WaveDefinition is a pure data Resource (no scene tree dependency)
  - WaveManager uses `load()` (dynamic path by index), not `preload()` (path not constant)
  - typed return values for testability
  - fail-fast if wave index is out of range (Category A)
- validation performed:
  - `get_diagnostics` clean on both files
  - `run_scene` to verify WaveManager._ready() doesn't crash
- pitfalls:
  - do not use Array[WaveDefinition] in .tres files — use Array[Resource] (typed array footgun)
  - do not add architecture (spawner logic, enemy selection) beyond what the task specifies
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
