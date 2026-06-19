# Examples and Validation

Use this file before finalizing a retro note.

## Example Trigger

Implementation of repeated enemy scenes leaked health values because mutable runtime state lived in a shared resource.

## Good Output Shape

```md
# Retro Note

- context: enemy instancing task
- expected: each enemy instance would hold independent current health
- actual: changing one enemy health affected other instances
- evidence: Enemy.tscn instances shared data through EnemyConfig.tres during test
- root cause: mutable runtime state was stored in a shared Resource reused through scene instancing
- impact: future enemy/resource tasks would keep producing hidden shared-state bugs
- corrected rule: keep per-instance mutable gameplay state on nodes/components unless intentional duplication is applied
- target skill: godot-scene-resource
- memory action: add to `Active Decisions` unless a broader state-ownership rule already exists there
- patch applied: godot-scene-resource/SKILL.md Failure Modes — added "Do not store per-instance mutable gameplay state in shared Resources without explicit duplication"
```

## Recurrence Escalation Example

If `MEMORY.md` already says "mutable runtime state on shared Resources leaks across instances" and a new task hits the same bug:

```md
# Retro Note

- context: pickup instancing task
- expected: each pickup tracks its own collected state
- actual: collecting one pickup marked all instances as collected
- evidence: PickupData.tres shared across Pickup.tscn instances
- root cause: same as MEMORY.md Known Quirks entry — shared Resource mutable state
- impact: memory entry did not prevent recurrence; the rule must live in the skill
- corrected rule: add explicit warning to godot-scene-resource Failure Modes and executor-checklist Common Footguns
- target skill: godot-scene-resource
- memory action: none — already in MEMORY.md, escalating to skill patch
- patch applied: godot-scene-resource/SKILL.md Failure Modes — added rule; godot-gdscript/references/executor-checklist.md Common Footguns — added warning
```

## Validation Checklist

- note captures root cause, not just symptom
- corrected rule is reusable
- target skill is explicit
- evidence is specific enough to justify a future skill update
- any `MEMORY.md` update is explicit and non-contradictory
- if the pattern already exists in `MEMORY.md`, the note escalates to a skill patch instead of adding another memory entry
- patch-applied field is present (either the patch or "deferred" with reason)

## Bad Signs

- "task failed" with no broken assumption named
- unresolved guesses written as facts
- no target skill
- a duplicate memory rule instead of refining the existing one

## Escalate

Do not write the note yet if the root cause is still unknown.
