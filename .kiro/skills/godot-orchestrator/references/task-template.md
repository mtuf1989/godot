# Task Template

Use this template to keep work atomic and handoffs reliable.

## Atomic Task Rules

A task is atomic only if it has:

- one clear goal
- explicit outputs
- bounded touch points
- one validation method
- a clear owner skill

If a task depends on editor-connected MCP execution, it is atomic only if it also names:

- required MCP resource reads
- ordered MCP tool calls (including `find_nodes` for targeted lookups, `duplicate_node` for cloning, `editor_screenshot` for visual verification when applicable)
- sync steps such as `refresh_filesystem` or `save_scene`
- approval gates such as `delete_node` or `none`

If a task needs both architecture decisions and coding, split it.

## Canonical Template

```md
## T<id>: <title>
- goal: <single concrete outcome>
- targets:
  - <file, scene, doc, or artifact>
- depends_on:
  - <task id or none>
- preconditions:
  - <approved decision, required file, required connectivity, or none>
- context_to_read:
  - <exact file, scene, setting, or artifact to inspect before acting>
- mcp_context:
  - <required MCP resource reads, or none>
- mcp_tools:
  - <ordered MCP tool calls, or none>
- sync_steps:
  - <refresh/save/import steps, or none>
- validation:
  - <how success is checked>
- approval_requirements:
  - <required human approval gate, or none>
- blocked_if:
  - <exact blocker and the required next action>
- skills_to_activate:
  - <skill name>
- traces:
  - <back-reference to scoped brief criteria, architect property, or GDD requirement, or none>
- error_notes:
  - <architect error classification for error paths this task touches, or none>
```

### Optional Task Convention

Append `*` to the task title when the task adds value but is not required for the feature to function:

```md
## T5*: Write property test for cache bounded size
```

Common optional tasks: property-based tests, integration tests, performance benchmarks, demo scenes, debug tooling. The executor can skip `*` tasks for faster MVP delivery.

## Routing Defaults

- scope reduction -> `godot-scope`
- architecture choice -> `godot-architect`
- scene/resource safety -> `godot-scene-resource`
- save/load design -> `godot-persistence`
- CommonUI navigation (UIActivatable, UIRouter, transitions, SceneRouter, glyphs, themes) -> `godot-common-ui`
- Control layout, anchors, containers, theme styling without CommonUI -> `godot-ui-core`
- procedural UI shapes (SDF, ProceduralPanel2D, style libraries) -> `godot-procedural-ui`
- 2D canvas_item shader effects (hit flash, dissolve, outline, palette swap) -> `godot-shader-canvasitem-fx`
- dialogue files, balloon scenes, DialogueManager wiring -> `godot-dialogue`
- audio playback, music, ambient sounds, SoundManager wiring -> `godot-sound`
- behavior trees, HSM, NPC/enemy AI with LimboAI -> `godot-limboai`
- GdUnit4 tests -> `godot-gdunit4`
- placeholder 2D prototype art -> `godot-prototype-assets-2d`
- 3D blockout / greybox level geometry -> `godot-blockout-3d`
- GDExtension C++ native code -> `godot-gdextension-cpp`
- bounded GDScript implementation -> `godot-gdscript`
- lessons learned -> `godot-retro`

Executor tasks implicitly inherit the project steering files (`GODOT-CODING-RULES.md`, `GODOT-MCP-PROTOCOL.md`).
Use `none` for the MCP-specific fields when the task is fully filesystem-only.

## Good vs Bad

Bad:

- "Build combat system and inventory and save/load."

Good:

- combat loop task
- inventory state task
- persistence design task

## Sequencing Rules

Typical order:

1. scope
2. architecture
3. specialist design tasks
4. bounded implementation with explicit preconditions and context reads
5. retro if assumptions broke

## Batch Execution Rule

When a plan has more than 5 tasks, group them into batches of 3-5. Each batch boundary is a hard validation gate:

- Run diagnostics on all files changed in the batch.
- Update `SESSION_PLAN.md` with batch completion status.
- Do not start the next batch until the current one validates cleanly.

This prevents large plans from being executed as a single pass where early errors cascade undetected through later tasks.

## SESSION_PLAN.md Convention

When the task list has more than two tasks, write it to a `SESSION_PLAN.md` file in the project root. This is a persistent recovery artifact — if the agent loses context mid-session, it can read this file to find the next ready task instead of re-planning from scratch.

Each task carries a `status` field: `pending`, `in_progress`, `done`, or `blocked`.

Update rules:
- mark `in_progress` before starting a task
- mark `done` after validation passes
- mark `blocked` with the exact blocker if a task cannot proceed

On session resume, read `SESSION_PLAN.md` first and find the next task whose status is `pending` and whose `depends_on` tasks are all `done`.

## Version Control Convention

When the project uses git, recommend committing after each validated task completes. This creates cheap rollback points so a failure in a later task does not require re-implementing earlier work.

Suggested commit message format:

```
T<id>: <task title>
```

## Escalate

Re-open architecture instead of patching the task list when:

- a task's preconditions can no longer be satisfied by the current architecture
- two tasks now conflict on ownership of the same scene, resource, or subsystem
- validation criteria no longer match the chosen approach
- a task discovers that a core pattern chosen by the architect does not work in practice

When escalating, mark the affected tasks as `blocked` in `SESSION_PLAN.md` with the reason, then route to `godot-architect`.
