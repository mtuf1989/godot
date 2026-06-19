---
name: godot-persistence
description: |
  Design Godot save/load, durable identity, and state reconstruction safely.
  Use when a task involves save files, save games, load games, player progress, checkpoints, autosave, persistence, migration, serialization, or restoring runtime-created entities.
  Also use when someone asks "how do I save the game," "how do I load player data," "how should I store progress," "save system design," or encounters issues with node paths breaking across saves, instance IDs changing, or runtime-spawned objects disappearing after reload.
  If the feature needs any form of cross-session state survival, this skill should fire.
---

# Godot Persistence

Design persistence as a stable state layer, not as naive object serialization.

Read `references/persistence-decision-table.md` first. Read `references/examples-and-validation.md` before writing the persistence brief.

## Responsibility

- Choose safe save/load structure, identity strategy, and reconstruction flow.
- Prevent fragile reliance on node paths, instance IDs, or unsafe object serialization.
- Define migration and validation choices before implementation starts.
- Fit the design to the current project's actual entity, content, and file-location constraints.
- When immutable project assets must be referenced durably, prefer MCP UID resolution over hand-maintained path guesses when editor-connected.

## Use When

- The feature needs save/load.
- Runtime-spawned entities must persist across sessions.
- Cross-entity references or durable content references are required.
- The team needs versioning or migration strategy.

## Do Not Use When

- The problem is only scene/resource sharing during a single runtime.
- The task is generic gameplay implementation with no persistence.
- Scope and architecture are still unresolved.

## Workflow

1. Read `references/persistence-decision-table.md`, then inspect the actual project surfaces involved:
   - entity creation paths
   - world state owners
   - relevant autoloads or services
   - current content identifiers and resource paths
   Identify what must persist:
   - player/system progress
   - world state
   - runtime-spawned entities
   - cross-entity links
2. Choose a persistence model:
   - snapshot
   - hybrid snapshot + journal
   - other only if justified
3. Define durable identity and storage location:
   - stable entity ids for runtime objects
   - resource UIDs for project assets when appropriate, using `project_path_to_uid` or `uid_to_project_path` when editor-connected
   - `user://` for persistent files unless there is a strong reason otherwise
4. Define load phases:
   - base world
   - recreate dynamic entities
   - apply state
   - resolve links
5. Read `references/examples-and-validation.md`, then record schema versioning and failure behavior.

## Output

Produce a persistence design brief containing:

- what persists and what does not
- storage format recommendation
- storage location
- durable identity scheme
- MCP UID resolution used when relevant
- load/reconstruction phases
- migration/versioning note
- validation and corruption-handling checks

## Quality Gates

- The design avoids node-path and instance-id fragility.
- Runtime-spawned entities have durable identity.
- Load order and reference resolution are explicit.
- Versioning is accounted for from the start.
- On-disk location and current-project content references are explicit.
- Asset references use stable UIDs when that durability matters and editor-connected UID resolution is available.

## Failure Modes

- Do not serialize raw object graphs by default.
- Do not rely on `NodePath` as durable identity.
- Do not mix settings and save-game concerns without reason.
- Do not ignore resource cache behavior when using Resource-based saves.
- Do not write persistent save data into `res://` by default.

## References

Read only as needed:

- `references/persistence-decision-table.md`
- `references/examples-and-validation.md`
- `../../foundation/Research_SaveLoad.md`
- `../../foundation/Research_Resource.md`
- `../../foundation/Godot Nuanced Development Practices.md`
