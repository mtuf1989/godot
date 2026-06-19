---
name: godot-limboai
description: |
  Build Game AI for Godot 4 using LimboAI — behavior trees, hierarchical state machines,
  and blackboard-based data sharing. Use this skill whenever the user wants to create
  NPC behavior, enemy AI, game agents, behavior trees, state machines, patrol logic,
  combat AI, or any game AI system in Godot Engine. Also trigger when the user mentions
  LimboAI, BTTask, BTAction, BTCondition, BTPlayer, LimboHSM, LimboState, BTState,
  blackboard variables, or asks to design AI decision-making for game characters.
  Even if the user just says "make the enemy smarter" or "add AI to this character,"
  this skill applies.
---

# Godot LimboAI

Implement game AI using LimboAI behavior trees, hierarchical state machines, and blackboard data sharing.

Read `references/limboai-core.md` first. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the actual project surfaces the AI touches before editing.
- Design and implement behavior trees, HSMs, or hybrid BT+HSM systems using LimboAI.
- Write custom BTAction and BTCondition tasks in typed GDScript with `@tool` support.
- Configure blackboard plans, variable scoping, and BBParam bindings.
- Use built-in tasks before writing custom ones.
- Use native file editing for scripts and `.tres` resources (see `references/tres-authoring.md` for BehaviorTree format), then MCP tools for scene assembly when editor-connected.
- Validate with the strongest available method and report limits honestly.

## Use When

- The task requires NPC behavior, enemy AI, or agent decision-making.
- The project uses or should use LimboAI for behavior trees or state machines.
- Custom BT tasks, HSM states, or blackboard schemas need implementation.

## Do Not Use When

- The AI design decision (pure BT vs HSM vs hybrid) is still undecided — route to `godot-architect`.
- The task is general GDScript implementation unrelated to LimboAI.
- The task is animation state machines (AnimationTree, StateMachinePlayback), sprite animation, or procedural animation rather than AI behavior — route to `godot-animation`.
- The main issue is scene/resource ownership of `.tres` BehaviorTree files — route to `godot-scene-resource`.
- Scope is still broad or architecture is unapproved.

## Workflow

1. Read `references/limboai-core.md`, then inspect the project surfaces:
   - existing `res://ai/` folder structure, custom tasks, and `.tres` tree resources
   - BTPlayer or LimboHSM nodes already in scenes (use `find_nodes` with type "BTPlayer" or "LimboHSM" for targeted lookups)
   - blackboard plans and variable conventions in use
   - the agent node the AI will control and its capabilities
   - physics layers, groups, and signals the AI needs to interact with
   In editor-connected mode, use `search_symbols`, `get_definition`, or `get_hover_info` to resolve LimboAI API questions.

2. Choose the AI architecture pattern (if already decided by `godot-architect`, follow that decision):
   - **Pure BT** — single behavioral mode with branching decisions
   - **Pure HSM** — clearly state-based with few decisions per state
   - **BT + HSM hybrid** — distinct phases with complex decisions per phase

3. Implement using these defaults unless the task states otherwise:
   - `@tool` on every task that defines `_generate_name()`
   - typed variables and return types
   - `_var` suffix on blackboard variable name properties
   - `LimboUtility.decorate_var()` in `_generate_name()`
   - `is_instance_valid()` before accessing object variables from blackboard
   - built-in tasks from `references/builtin-tasks.md` before writing custom ones
   - read `references/ai-patterns.md` for common pattern implementations

4. Organize files following project conventions or default to:
   ```
   res://ai/tasks/actions/    # Custom BTAction scripts
   res://ai/tasks/conditions/  # Custom BTCondition scripts
   res://ai/trees/             # .tres BehaviorTree resources
   ```

5. For `.tres` BehaviorTree resources, prefer text authoring (read `references/tres-authoring.md`) when:
   - Creating new trees from scratch
   - Batch-generating multiple tree variants
   - Edit exist trees
   Use the editor only when user state that they want to hand-craft it

6. In editor-connected mode, call `refresh_filesystem` after each changed `.gd` or `.tres` file.

7. Run the strongest available validation:
   - editor-connected: `get_diagnostics` on changed scripts, verify BTPlayer/LimboHSM node setup via `scene://current/tree` or `find_nodes`
   - source-only: state the exact editor validation step still required

8. Escalate if needed:
   - architecture uncertainty → `godot-architect`
   - BehaviorTree `.tres` sharing/ownership issues → `godot-scene-resource`
   - save/load of AI state across sessions → `godot-persistence`
   - general GDScript issues outside LimboAI → `godot-gdscript`

## Output

Produce bounded AI implementation work that includes:

- files and scenes changed or created
- BT structure summary (tree shape in text)
- custom tasks written and their responsibilities
- blackboard variables defined
- MCP sync or LSP steps used when editor-connected
- validation performed and its limits
- exact blocker and next action if blocked

## Quality Gates

- Custom tasks use `@tool` when defining `_generate_name()`.
- Blackboard variable properties use `_var` suffix.
- Built-in tasks are preferred over custom equivalents.
- Object variables from blackboard are guarded with `is_instance_valid()`.
- The implementation is grounded in actual project artifacts.
- Expensive operations are cached in `_setup()` or `_enter()`, not `_tick()`.
- `distance_squared_to()` is preferred over `distance_to()` in conditions.

## Failure Modes

- Do not write custom tasks that duplicate built-in task functionality.
- Do not skip `@tool` on tasks with `_generate_name()`.
- Do not type-hint nullable object variables from the blackboard.
- Do not put heavy computation in `_tick()`.
- Do not access parent blackboard scope directly — use variable mapping in BTState.
- Do not create monolithic trees — use `BTSubtree` for reusable subtrees.
- Do not make architecture decisions during implementation — escalate to `godot-architect`.

## References

Read only as needed:

- `references/limboai-core.md`
- `references/builtin-tasks.md`
- `references/ai-patterns.md`
- `references/examples-and-validation.md`
- `references/tres-authoring.md` — read when generating or editing `.tres` BehaviorTree files as text (without the editor). Covers file grammar, value syntax, all sub-resource patterns, and composition principles.
- `../../foundation/Godot Nuanced Development Practices.md`
