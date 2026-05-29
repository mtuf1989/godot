# Examples and Validation

Use this file before finalizing the persistence brief.

## Example Input

Feature:

- runtime-spawned loot chests
- opened/closed state must survive across sessions
- content definitions live in project assets

## Good Output Shape

```md
# Persistence Brief

- persists:
  - chest opened state
  - spawn records for runtime-created chests
- format:
  - snapshot-based save for this slice
- storage location:
  - `user://saves/slot_01.save`
- identity scheme:
  - durable chest ids for spawned entities
  - resource UID or stable content reference for chest prototype
  - use `project_path_to_uid` when editor-connected so the saved asset reference does not rely on a hand-copied path guess
- load phases:
  - load base world
  - recreate runtime chests
  - apply opened state
  - resolve links if any
- migration note:
  - include format version now
- validation checks:
  - loading the same save twice produces the same opened/closed world state
```

## Validation Checklist

- no `NodePath` used as durable identity
- runtime objects have stable ids
- asset references use UIDs when that durability matters and the editor-connected toolchain can resolve them
- load order is explicit
- migration/version header is included
- settings are not mixed into save data casually
- save location is explicit and not left as an implicit `res://` default

## Bad Signs

- serializing raw object graphs by default
- relying on instance ids
- writing persistent files into `res://` without a strong reason
- no corruption or missing-content stance

## Escalate

Consult `godot-scene-resource` if reconstruction depends on tricky resource duplication or ownership behavior.
