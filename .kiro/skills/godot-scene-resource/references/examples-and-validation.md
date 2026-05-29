# Examples and Validation

Use this file before finalizing the scene/resource note.

## Example Input

Feature:

- `Enemy.tscn` is instanced many times
- each enemy references `EnemyConfig.tres`
- implementation wants to mutate cooldown and health through the resource

## Good Output Shape

```md
# Scene/Resource Note

- affected artifacts: Enemy.tscn, EnemyConfig.tres, enemy_controller.gd
- grounding method: `open_scene` plus `scene://current/tree` if the current enemy scene hierarchy or ownership chain matters
- shared state: enemy tuning data such as base_speed and base_damage in `EnemyConfig`
- unique state: current_health and current_cooldown on each enemy node/component
- ownership model: scene instances own mutable runtime state; config resource stays shared
- duplication/local_to_scene decision: do not rely on shared mutable Resource; only duplicate if a truly per-instance resource object is required
- validation check: instantiate two enemies, change one runtime value, confirm the other does not change
```

## Validation Checklist

- shared vs unique state is named explicitly
- current artifacts are named explicitly
- recommendation accounts for cache reuse and scene instancing
- editor-connected hierarchy claims are grounded in a real scene-tree read when needed
- validation check would catch leakage
- local-to-scene is not treated as magic

## Bad Signs

- mutable gameplay state kept inside a shared `.tres`
- no concrete scene/resource/script named
- recommendation relies on node duplication alone
- no validation step

## Escalate

Consult `godot-persistence` if the same state must be restored later across save/load.
