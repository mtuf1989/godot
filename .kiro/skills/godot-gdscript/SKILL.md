---
name: godot-gdscript
description: |
  Execute bounded Godot implementation tasks in typed GDScript with engine-safe conventions and common pitfall avoidance.
  Use when the task is already scoped, architected, and ready for coding.
  Also use when someone says "write this script," "implement this feature," "code this system," "fix this GDScript," or needs help with typed GDScript syntax, signal connections, lifecycle callbacks, physics vs process, preload vs load, or any concrete Godot 4 scripting work.
  If the user has a clear implementation target and needs reliable GDScript code rather than architecture advice, this skill should fire.
---

# Godot GDScript

Implement clear Godot tasks in typed GDScript with minimal coherent changes, explicit validation, and no silent redesign.

Read `references/executor-checklist.md` first. Read `references/examples-and-validation.md` before finalizing implementation guidance.

## Responsibility

- Inspect actual targets and nearby integration points before editing.
- Write bounded gameplay or system code in Godot-safe GDScript.
- Apply typed syntax, lifecycle discipline, and reliable engine conventions.
- Avoid common errors that create parser, runtime, or scene-integration failures.
- Use native file editing for script changes, then use MCP refresh and LSP tools when editor-connected validation is available.
- Validate with the strongest available method and report limits honestly.

## Use When

- The task already has clear targets and acceptance criteria.
- The architecture and scene/resource decisions are already made.
- The work belongs in GDScript rather than higher-risk runtimes.

## Do Not Use When

- Scope is still broad.
- Core architecture is undecided.
- The main issue is save/load design, scene/resource ownership, or language strategy.
- The task is primarily about animation systems (AnimationPlayer, AnimationTree, state machines, sprite animation, procedural animation, IK, combos) — route to `godot-animation`.

## Workflow

1. Read `references/executor-checklist.md`, then inspect the task brief, exact targets, adjacent integration points, and required project settings before editing. In editor-connected mode, use `search_symbols`, `get_definition`, `get_hover_info`, or `find_nodes` when existing symbols, engine APIs, or scene node locations are still unclear after local inspection.
2. Confirm preconditions:
   - target files/scenes exist or the task explicitly allows creating them
   - required node types, callbacks, groups, input actions, autoloads, and scene/resource assumptions are known
   - architecture and specialist design decisions are already approved
   - the current validation mode is known: editor-connected, headless/static, or source-only
3. Read `references/examples-and-validation.md`, then implement the smallest coherent change with these defaults unless the task states otherwise:
   - typed variables and return types
   - `_physics_process()` for movement and deterministic mechanics
   - `_process()` for visual-only behavior
   - choose `preload()`, `load()`, or threaded loading based on known usage and hitch tolerance; do not default blindly
   - modern Godot 4 signal syntax
4. Use native file editing for `.gd` files and other text-heavy supporting files. If the task also touches editor-owned scene state, keep that work limited to the explicit MCP steps already approved by the task rather than silently mutating live scene structure on disk.
5. In editor-connected mode, call `refresh_filesystem` after each changed `.gd` file or other editor-consumed file that must be reloaded before validation.
6. Run the strongest available validation loop and fix surgically if errors appear:
   - editor-connected: run `get_diagnostics` on each changed script first, and use `search_symbols`, `get_hover_info`, or `get_definition` to resolve integration issues before widening the edit
   - headless/static: use the strongest engine-backed static check available
   - source-only: end with the exact editor validation step still required
7. Escalate back to `godot-architect`, `godot-scene-resource`, or `godot-persistence` if the task assumptions break.

## Output

Produce bounded implementation work that includes:

- files, scenes, or nodes changed
- MCP sync or LSP steps used when editor-connected
- key callbacks and signals
- critical engine conventions
- validation performed and its limits
- exact blocker and next action if blocked

## Quality Gates

- The task stays within the approved scope.
- The change is grounded in actual project artifacts, not only in the task summary.
- Code guidance is typed and Godot 4 aligned.
- Hidden architecture changes are surfaced instead of improvised.
- Common parser/runtime footguns are avoided.
- Error handling follows the foundation fail-fast philosophy: crash on programmer errors, degrade visibly on asset errors, handle only what the architect explicitly classified as Category C.
- Editor-connected work uses `refresh_filesystem` before trusting editor diagnostics on changed files.
- Validation claims match the real validation mode used.

## Failure Modes

- Do not turn this skill into a giant syntax cookbook.
- Do not choose new architecture during implementation.
- Do not use outdated Godot 3 idioms when Godot 4 behavior differs.
- Do not reach for non-GDScript runtimes without explicit architectural approval.
- Do not emit only “guidance” when the task is executable and writable targets are available.
- Do not treat stale editor state as current if `refresh_filesystem` was never requested after disk edits.
- Do not claim runtime or editor validation that was not actually performed.

## References

Read only as needed:

- `references/executor-checklist.md`
- `references/examples-and-validation.md`
- `references/mass-entity-optimization.md`
- `../../foundation/Godot Language Strategy Guide.md`
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
