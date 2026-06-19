---
name: godot-retro
description: |
  Capture Godot-specific failures, wrong assumptions, and missing heuristics during or after implementation or planning work.
  Use after a task fails, a phase completes with surprises, an assumption breaks, a Godot quirk is discovered, or something went wrong that should not happen again.
  Also use mid-session when a task reveals a workaround or discovery that later tasks in the same session need to know about — log it to MEMORY.md immediately rather than waiting for a post-mortem.
  If someone says "this broke," "why did this fail," "lesson learned," "what went wrong," "post-mortem," or "we should remember this," this skill should fire.
---

# Godot Retro

Turn mistakes and surprises into structured improvements for the skill collection.

Read `references/retro-template.md` first. Read `references/examples-and-validation.md` before writing the final note.

## Responsibility

- Record what went wrong, why it went wrong, and which skill needs improvement.
- Separate one-off execution mistakes from reusable lessons.
- Curate the project's `MEMORY.md` when the lesson should affect future work in the same repo.
- Feed the next revision of the skill set with concrete evidence.
- Produce evidence strong enough that the affected skill can actually be patched later.

## Use When

- A task failed or required major replanning.
- An implementation exposed a false assumption in planning or architecture.
- A Godot quirk or edge case was discovered during execution.
- A phase completed and there is reusable process learning to keep.
- Mid-session: a task reveals a workaround, discovery, or Godot quirk that later tasks in the same session need to know about. Do not wait for a post-mortem — log it to `MEMORY.md` immediately so the information is available for subsequent tasks.

## Do Not Use When

- Nothing meaningful was learned beyond routine completion.
- The issue is still under active debugging and root cause is unknown.
- The task only needs a status update, not a retrospective.

## Workflow

1. Read `references/retro-template.md`, then capture the trigger event:
   - task
   - phase
   - failure
   - mid-session discovery
2. If the current project has a `MEMORY.md`, read the related entries first so you do not duplicate or contradict active guidance.
3. Record:
   - expected behavior
   - actual behavior
   - evidence
   - root cause
   - workaround or fix
4. Decide whether the lesson belongs to:
   - `godot-scope`
   - `godot-architect`
   - `godot-orchestrator`
   - `godot-scene-resource`
   - `godot-persistence`
   - `godot-gdscript`
   - `godot-ui-core`
   - `godot-common-ui`
   - `godot-dialogue`
   - `godot-sound`
   - `godot-feel`
   - `godot-limboai`
   - `godot-gdunit4`
   - `godot-shader-canvasitem-fx`
   - `godot-procedural-ui`
   - `godot-prototype-assets-2d`
   - `godot-blockout-3d`
   - `godot-gdextension-cpp`
5. Decide whether the lesson should also update `MEMORY.md`:
   - add a new active rule
   - replace a stale active rule
   - move an old rule into `Superseded Decisions`
   - no project-memory change
   - **recurrence escalation**: if `MEMORY.md` already contains this pattern (under Known Quirks, Proven Patterns, or Active Decisions) and the same lesson is surfacing again, do not write another memory entry. Instead, escalate: propose a concrete patch to the target skill's SKILL.md or references so the lesson becomes a permanent rule, not a recurring memory note.
6. For mid-session discoveries: if the finding is stable enough to act on now and later tasks depend on it, update `MEMORY.md` immediately under `Proven Patterns` or `Known Quirks And Errors` rather than waiting for a full retro note. A one-line entry with the task context is enough. The full retro note can follow later if the lesson is broader.
7. Read `references/examples-and-validation.md`, then write a structured improvement note and update `MEMORY.md` if the lesson is stable enough.
8. **Close the loop**: if the corrected rule is concrete enough to apply now and the target skill is in this repo, apply the patch to the target skill's SKILL.md or references in the same session rather than deferring. A retro note that names a fix but never applies it is a dead end.
9. If there is no reusable lesson, explicitly say so and stop.

## Output

Produce a compact improvement note containing:

- context
- wrong assumption or missing heuristic
- evidence
- impact
- corrected rule
- target skill to update
- memory action, when `MEMORY.md` should change
- patch applied, or "deferred" with reason if not concrete enough yet

## Quality Gates

- The note is reusable, not just a complaint log.
- Root cause is distinguished from symptoms.
- The target skill to improve is explicit.
- The lesson is concrete enough to modify a skill later.
- Evidence is specific enough to justify the correction.
- Any `MEMORY.md` change removes contradiction instead of creating a second active rule.
- If the pattern already exists in `MEMORY.md`, the note escalates to a skill patch instead of adding another memory entry.
- If the corrected rule was concrete and the target skill was local, the patch was applied in the same session.

## Failure Modes

- Do not create a retrospective entry for routine execution noise.
- Do not log unresolved guesses as facts.
- Do not stop at “task failed”; explain which assumption broke.
- Do not bury the skill-improvement target.
- Do not propose a skill correction without concrete evidence.
- Do not append duplicate `MEMORY.md` guidance when the rule already exists.
- Do not keep writing memory entries for the same recurring pattern — escalate to a skill patch.
- Do not defer a skill patch when the corrected rule is concrete and the target skill is in this repo.

## References

Read only as needed:

- `references/retro-template.md`
- `references/examples-and-validation.md`
- `../../foundation/Research_Resource.md`
- `../../foundation/Research_SaveLoad.md`
- `../../foundation/Godot Nuanced Development Practices.md`
