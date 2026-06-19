# Resource Decision Table

Use this file to decide where state lives and how scene instances stay isolated.

## Project Grounding

Before deciding:

- inspect the actual `.tscn`, `.tres`, and script consumers involved
- name the concrete owner node or resource for each important state bucket
- confirm whether the resource is built-in, disk-backed, duplicated, or created at runtime

## State Placement Table

| Situation | Default Home | Why |
|---|---|---|
| Mutable per-entity runtime state | Node property or component node | Avoid hidden shared resource state |
| Shared configuration reused by many entities | `Resource` | Good fit for immutable or rarely mutated data |
| Serialized data object reused across scenes | `Resource` with clear ownership | Keeps data separate from node graph |
| Cross-entity runtime lookup | Registry/service object | Avoid brittle path dependence |

## Duplication Table

| Need | Recommended Move | Warning |
|---|---|---|
| One shared config for all instances | Reuse same `Resource` | Do not mutate at runtime casually |
| Per-instance mutable copy | Explicit duplication or node-owned state | Verify no leakage across instances |
| Scene-instanced local resource | `resource_local_to_scene` works for simple cases; `duplicate_deep()` for complex graphs | Array/dict duplication fixed in 4.6+; nested-scene edge cases may still apply — verify on your engine version |
| Deep graph copy | Use intentionally and sparingly | Deep duplication can be expensive or semantically surprising |

## Validation Checklist

After recommending a pattern, verify:

- two scene instances do not share mutable state unexpectedly
- built-in resources are not silently shared when uniqueness was expected
- duplication depth matches the actual graph shape
- arrays/dictionaries of resources are treated carefully

## Red Flags

- mutating a loaded `.tres` as if it were entity-local state
- relying on node duplication alone for uniqueness
- assuming `resource_local_to_scene` solves every instancing problem
- storing important mutable runtime state inside externally shared resources

## Output Template

```md
# Scene/Resource Note

- affected artifacts: <scene/resource/script paths>
- shared state: <...>
- unique state: <...>
- ownership model: <...>
- duplication/local_to_scene decision: <...>
- validation check: <...>
```

## Escalate

Consult `godot-persistence` when the same state must survive save/load across sessions.
