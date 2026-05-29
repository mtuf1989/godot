---
name: godot-orchestrator
description: |
  Convert approved Godot architecture into atomic implementation tasks with dependencies, validation, and skill routing.
  Use when the architecture is decided and the work needs a reliable execution plan, a task breakdown, a build order, or a step-by-step implementation sequence.
  Also use when someone says "break this down into tasks," "what do I build first," "create a plan," "give me the implementation steps," or when a design is approved and the next step is organizing the actual coding work across multiple files, scenes, or systems.
---

# Godot Orchestrator

Break approved design into atomic work that specialized Godot skills can execute safely.

Read `references/task-template.md` first. Read `references/examples-and-validation.md` before finalizing the task list.

## Responsibility

- Decompose architecture into implementation tasks.
- Assign the right skill to each task.
- Make tasks small enough that execution does not need to re-plan.
- Make preconditions, required context reads, validation paths, and blocked states explicit.
- Produce a persistent `SESSION_PLAN.md` artifact that tracks task status across turns so execution can recover if context is lost mid-session.

## Use When

- A scoped brief exists and architecture decisions are approved.
- The work needs dependency-aware task sequencing.
- Multiple skills must be coordinated without overlap.

## Do Not Use When

- Scope is still unclear.
- Architecture is still undecided.
- The request is already a single bounded implementation task.

## Workflow

1. Read `references/task-template.md`, then read the scoped brief and architecture summary.
2. Read `references/examples-and-validation.md`, then decompose into atomic tasks. Each task must include:
   - `goal`
   - `targets`
   - `depends_on`
   - `preconditions`
   - `context_to_read`
   - `mcp_context`
   - `mcp_tools`
   - `sync_steps`
   - `validation`
   - `approval_requirements`
   - `blocked_if`
   - `skills_to_activate`
   - `traces` (optional: references back to scoped brief success criteria, architect correctness properties, or GDD requirements that this task validates)
   Use `none` for the MCP-specific fields when the task is filesystem-only.
3. Keep tasks narrowly scoped:
   - one problem
   - one clear output
   - limited file or subsystem touch points
   - one validation path
   - one owner skill
4. Route specialty concerns explicitly:
   - ANY UI screen, popup, menu, HUD, dialog, loading screen, level/world transition, screen flow, or navigation wiring -> `godot-common-ui` (default UI workflow — only defer to godot-ui-core for pure layout/anchors/containers/styling with zero navigation)
   - scene/resource risks -> `godot-scene-resource`
   - persistence work -> `godot-persistence`
   - dialogue files, balloon scenes, DialogueManager wiring -> `godot-dialogue`
   - audio playback, music crossfade, ambient sounds, SoundManager wiring -> `godot-sound`
   - game feel, juice, feedback sequences, screen shake, hit pause -> `godot-feel`
   - Control layout, anchors, containers, theme styling with zero navigation component -> `godot-ui-core`
   - procedural UI shapes (SDF, ProceduralPanel2D, style libraries) -> `godot-procedural-ui`
   - 2D canvas_item shader effects (hit flash, dissolve, outline, palette swap) -> `godot-shader-canvasitem-fx`
   - 3D spatial shader materials (water, terrain, foliage, skin, hair, eyes, glass, hologram) and Compositor post-processing (bloom, DOF, motion blur, outlines, tonemap) and advanced spatial techniques (POM, triplanar, stencil, custom depth, multi-pass) -> `godot-shader-spatial`
   - 2D VFX (particles, trails, flipbooks, screen-space post-processing, Tween-based procedural VFX, CanvasGroup compositions) -> `godot-vfx-2d`
   - 3D VFX (GPUParticles3D, volumetric fog, spatial VFX shaders, sub-emitters, stylized/anime VFX) -> `godot-vfx`
   - raw RenderingDevice compute shaders (boids, SPH fluid, spatial hashing, GPU-driven culling, procedural mesh generation, terrain generation/erosion, indirect dispatch, compute→render bridges) -> `godot-compute`
   - behavior trees, HSM, NPC/enemy AI with LimboAI -> `godot-limboai`
   - GdUnit4 tests -> `godot-gdunit4`
   - placeholder 2D prototype art -> `godot-prototype-assets-2d`
   - 3D blockout / greybox level geometry -> `godot-blockout-3d`
   - GDExtension C++ native code -> `godot-gdextension-cpp`
   - camera rigs, module pipelines, rig switching, blending, shake, teleport protocol, camera accessibility -> `godot-camera`
   - structured logging, GLog.info/event/transition calls, session lifecycle, LLM log export -> `godot-logger`
   - bounded GDScript coding -> `godot-gdscript`
   - animation systems (AnimationPlayer, AnimationTree, state machines, sprite animation, procedural animation, IK, retargeting, combos) -> `godot-animation`
   - physics bodies, collision layers/masks, raycasting, joints, ragdolls, physics interpolation, movement controllers, hitbox/hurtbox, Jolt configuration -> `godot-physics`
   Encode the actual execution split instead of leaving it implicit:
   - direct file editing for scripts or text-heavy assets
   - MCP resources and tools for editor-owned scene changes
   - `find_nodes` for targeted node lookups by name, type, or group instead of full `scene://current/tree` reads when the search target is known
   - `duplicate_node` for cloning existing scene structures instead of manually recreating them with `add_node` + `update_property`
   - `editor_screenshot` for visual verification of the running game when visual correctness matters
   - `refresh_filesystem` and `save_scene` only where the task actually needs them
   - approval-gated destructive steps called out explicitly
5. **Batch execution for large plans.** When a plan has more than 5 tasks, group them into batches of 3-5 tasks. Between batches:
   - Validate all tasks in the completed batch (diagnostics pass, no regressions).
   - Update `SESSION_PLAN.md` with batch status and any issues found.
   - Do not start the next batch until the current batch is validated.
   This prevents the failure mode where a large plan is executed as one pass, bypassing per-task validation and allowing early errors to cascade silently through later tasks. The batch boundary is a hard validation gate, not a suggestion.
6. **Optional tasks.** Mark tasks with `*` when they add value but are not required for the feature to function. Common optional tasks: property-based tests for correctness properties, integration tests, performance benchmarks, demo scenes. The executor can skip `*` tasks for faster MVP delivery. Non-optional tasks must all pass for the feature to be considered complete.
7. **Correctness property tasks.** When the architect defines correctness properties, generate a test task for each property. These tasks should:
   - reference the property by name and number
   - target the gdunit4 skill
   - be marked as optional (`*`) unless the property guards a critical invariant
   - include the property statement as the acceptance criterion
   Group property test tasks after the implementation tasks they validate, not at the end of the plan.
8. **Error classification propagation.** When the architect classifies error scenarios, propagate the classification into the relevant implementation tasks. Add a note to each task that touches an error path: "Error: <scenario> is Category <A/B/C> per architect. <expected response>." This prevents the implementation skill from inventing its own error handling strategy.
9. If execution reveals a broken assumption, re-open architecture instead of silently patching the plan.

## Session Plan Artifact

When the task list is non-trivial (more than two tasks), write it to a `SESSION_PLAN.md` file in the project root. This serves as a persistent recovery artifact so execution can resume if the agent loses context mid-session.

Each task in the plan should carry a status field: `pending`, `in_progress`, `done`, or `blocked`.

Update the status in `SESSION_PLAN.md` as tasks progress:
- mark `in_progress` before starting a task
- mark `done` after validation passes
- mark `blocked` with the exact blocker if a task cannot proceed

If the session resumes after a context break, read `SESSION_PLAN.md` first to find the next ready task (pending, with all dependencies done) instead of re-planning from scratch.

## Escalation Triggers

Re-open architecture instead of patching the task list when any of these concrete conditions are met:

- a task's preconditions can no longer be satisfied by the current architecture
- two tasks now conflict on ownership of the same scene, resource, or subsystem
- validation criteria no longer match the chosen approach
- a task discovers that a core pattern chosen by the architect does not work in practice

When escalating, mark the affected tasks as `blocked` in `SESSION_PLAN.md` with the reason, then route to `godot-architect`.

## Version Control Convention

When the project uses git, recommend committing after each validated task completes. This creates rollback points so a failure in a later task does not require re-implementing earlier work. The commit message should reference the task id and title from the plan.

## Output

Produce an ordered task list where each task has:

- task id and title (append `*` to the title for optional tasks)
- goal
- targets
- depends_on
- preconditions
- context_to_read
- mcp_context
- mcp_tools
- sync_steps
- validation
- approval_requirements
- blocked_if
- skills_to_activate
- traces (optional: back-references to scoped brief criteria, architect properties, or GDD requirements)

## Quality Gates

- Tasks are atomic and testable.
- Dependencies are explicit.
- Each task names the correct skill instead of relying on a generic “do the work”.
- MCP-backed tasks name the exact resource reads, tool sequence, sync steps, and approval gates instead of making the executor invent them.
- The executor does not need to guess expected outputs, first inspection targets, or blocked-state behavior.
- A `SESSION_PLAN.md` exists for non-trivial plans and task statuses are kept current.
- Optional tasks are marked with `*` and grouped near the implementation tasks they extend.
- When the architect provides correctness properties, each property has a corresponding test task.
- When the architect classifies error scenarios, the classification is propagated to relevant implementation tasks.
- Traces back to source requirements or properties are present when the architect or scoped brief provides them.

## Failure Modes

- Do not produce vague phase labels without atomic tasks.
- Do not hide architecture decisions inside task descriptions.
- Do not assign one task to many unrelated skills.
- Do not keep tasks so broad that they trigger replanning during implementation.
- Do not emit tasks that depend on undefined project context or impossible validation.
- Do not mark critical-path tasks as optional. Only mark tasks that add value but are not required for the feature to function.

## References

Read only as needed:

- `references/task-template.md`
- `references/examples-and-validation.md`
