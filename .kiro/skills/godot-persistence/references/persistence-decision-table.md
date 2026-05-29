# Persistence Decision Table

Use this file before choosing a save/load approach.

## What Must Persist?

Classify state first:

- player or account progress
- world state
- runtime-spawned entities
- links between entities
- settings only

Keep settings separate from save-game data unless the user explicitly wants them combined.

## Format Defaults

| Need | Default | Why |
|---|---|---|
| Simple typed save data | Variant-based or resource-backed snapshot | Less custom encoding burden |
| Large evolving world state | Hybrid snapshot plus incremental records | Better for growth and autosave |
| Human-editable saves | JSON only if the tradeoff is intentional | Requires more manual encoding discipline |

## Storage Location Defaults

- use `user://` for persistent save/settings files
- keep shipped content and prototypes in `res://`
- if using resource-backed save files, account explicitly for resource cache semantics

## Identity Defaults

- use durable entity ids for runtime-spawned objects
- use resource UIDs for project assets when stable content references are needed
- do not use `NodePath` as durable identity
- do not use runtime instance ids across sessions

## Load Phase Template

1. load base world
2. recreate dynamic entities
3. apply saved state
4. resolve cross-entity links
5. run validation checks

## Versioning Checklist

- top-level format version exists
- migration path is named if schema may evolve
- behavior on missing content is defined
- corruption handling or fallback path is defined

## Output Template

```md
# Persistence Brief

- persists: <...>
- format: <...>
- storage location: <...>
- identity scheme: <...>
- load phases: <...>
- migration note: <...>
- validation checks: <...>
```

## Escalate

Consult `godot-scene-resource` when the saved state design depends on tricky resource ownership or duplication semantics during reconstruction.
