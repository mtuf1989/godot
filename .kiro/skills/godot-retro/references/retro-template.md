# Retro Template

Use this file only when there is a reusable lesson.

## What Qualifies

Write a retro note when:

- a planning assumption was wrong
- a Godot-specific quirk changed the approach
- the task or phase had to be re-planned for structural reasons
- a fix revealed a missing rule that should live in a skill
- a durable lesson should update the project's `MEMORY.md`

Do not write one for routine progress noise.

## Mid-Session Quick Log

Not every discovery needs a full retro note. When a task reveals a workaround or Godot quirk mid-session and later tasks depend on that knowledge, write a quick entry directly to `MEMORY.md` under `Proven Patterns` or `Known Quirks And Errors`:

```md
- [T3 context] CSG boolean operations on concave children produce broken collision — use primitive shapes instead.
```

One line with the task context is enough. The full retro note can follow later if the lesson turns out to be broader. The goal is to make the discovery available to subsequent tasks in the same session rather than losing it to context limits.

## Root Cause Filter

Do not stop at symptoms like:

- parse error
- bug happened
- task took longer

Keep going until you can name:

- what assumption failed
- why it failed
- what rule would have prevented it

If `MEMORY.md` exists, check whether that rule is already active before writing a new one.

### Cascade Failures

When one error produces many downstream errors (e.g., a base class parse failure causing "Could not resolve class" in every subclass), explicitly separate the single root cause from the N symptom errors. Log the root cause as the retro finding — not the symptoms. Note the cascade multiplier (e.g., "1 missing method in feedback.gd → 30+ errors across 12 subclasses") because it reveals which files are high-leverage validation targets.

## Canonical Note

```md
# Retro Note

- context: <task or phase>
- expected: <what we thought>
- actual: <what happened>
- evidence: <file, log, or observed behavior>
- root cause: <real cause>
- impact: <why this matters to future work>
- corrected rule: <new reusable rule>
- target skill: <which skill to update>
- memory action: <add | replace | supersede | none>
- patch applied: <file and one-line summary, or "deferred — not concrete enough yet">
```

## Recurrence Escalation

Before writing a new `MEMORY.md` entry, grep for the pattern:

```bash
grep -i "keyword" MEMORY.md
```

If the pattern is already there and you are writing the same lesson again, the memory layer has failed to prevent recurrence. Do not add another memory entry. Instead:

1. Name the target skill.
2. Write the concrete corrected rule as it should appear in that skill's SKILL.md or references.
3. Apply the patch in the same session if the skill is in this repo.

A lesson that lives only in `MEMORY.md` is a workaround. A lesson patched into the skill is a fix.

## Close the Loop

A retro note that names a target skill but never patches it is a dead end. When the corrected rule is concrete and the target skill is local:

1. Open the target skill's SKILL.md or the relevant reference file.
2. Add the rule to the appropriate section (Failure Modes, Quality Gates, Workflow, or a reference checklist).
3. Note the patch in the retro note: `- patch applied: <file> — <one-line summary>`

If the rule is not concrete enough to patch yet (still speculative, needs more evidence), say so explicitly and leave it in `MEMORY.md` as a provisional entry.

## Mapping Guide

- wrong slice size or hidden feature creep -> `godot-scope`
- wrong pattern choice -> `godot-architect`
- bad task granularity -> `godot-orchestrator`
- resource or instancing bug -> `godot-scene-resource`
- implementation convention failure -> `godot-gdscript`
- save/load identity or load-order failure -> `godot-persistence`

## Example

If a plan assumed a mutable `Resource` could hold per-enemy runtime state and scene instances leaked values across enemies, the target skill is `godot-scene-resource`, not `godot-gdscript`.
