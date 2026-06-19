---
name: godot-architect
description: |
  Decide Godot architecture, engine patterns, and language/runtime choices for a scoped feature or slice.
  Use when the task needs pattern selection, tradeoff analysis, or engine-aware technical direction.
  Also use when someone asks "how should I structure this," "what pattern should I use," "should I use signals or," "composition vs inheritance," "scene tree design," "should this be a Resource or a Node," "GDScript or C#," or needs help choosing between multiple viable Godot approaches.
  If the user is deciding how to organize scenes, manage state, pick a runtime, or design node hierarchies for a concrete feature, this skill should fire.
---

# Godot Architect

Choose the engine-appropriate approach that fits the current project before task breakdown or coding.

Read `references/decision-lenses.md` first. Read `references/examples-and-validation.md` before writing the decision summary.

## Responsibility

- Turn a scoped feature into architecture decisions with rationale.
- Pick composition patterns, scene boundaries, dependency style, and runtime/language strategy.
- Surface Godot-specific risks early so execution does not fight the engine.
- Ensure the chosen approach fits the current project's real constraints and artifacts.
- Make toolchain boundaries explicit when they affect execution, especially the split between direct file edits and MCP-driven editor operations.

## Use When

- A scoped feature still has multiple viable technical approaches.
- The implementation touches scene structure, resources, threading, signals, or language boundaries.
- The team needs explicit tradeoffs, not just a to-do list.

## Do Not Use When

- The main need is reducing product scope.
- The architecture is already approved and the next step is task decomposition.
- The task is purely bounded implementation in GDScript.

## Workflow

1. Read `references/decision-lenses.md`, then read the scoped brief and inspect the actual project surfaces the decision will affect:
   - existing scenes and scene boundaries
   - relevant scripts and resources
   - autoloads and input actions
   - project settings or platform assumptions that matter
   - active addons in `addons/` and `project.godot` — check Lens 0.5 to identify which domains are already covered by existing plugins and which specialist skills own them
   Identify the decision points after grounding those facts.
2. Evaluate options through Godot-specific lenses:
   - project fit and compatibility
   - composition vs inheritance
   - node graph vs data/resource model
   - GDScript vs other runtime boundaries
   - main-thread vs worker-thread responsibilities
   - scene tree vs server-level optimization only if justified
   - whether implementation will rely on direct file edits, MCP scene mutation, or both
   - whether a new scene requires a starter `.tscn` on disk before `refresh_filesystem` plus `open_scene`
3. Read `references/examples-and-validation.md`, then choose one approach and record:
   - recommendation
   - compatibility constraints preserved
   - execution-surface constraints preserved
   - alternatives rejected
   - risks and mitigations
4. For complex features, define the integration contracts:
   - write component interface signatures when multiple components must agree on an API
   - write pseudocode for algorithms where two developers could reasonably diverge
   - classify error scenarios using foundation categories (A: crash, B: visible degradation, C: handle)
   - state correctness properties as "for any X, Y shall hold" when violations would be subtle
5. Call out when `godot-scene-resource` or `godot-persistence` must be consulted.
   Also call out addon-specialist skills when the design touches their domain:
   - AI behavior trees or state machines -> `godot-limboai`
   - animation systems (AnimationPlayer, AnimationTree, sprite animation, procedural animation, IK, retargeting) -> `godot-animation`
   - dialogue, branching narrative -> `godot-dialogue`
   - audio playback, music, SFX -> `godot-sound`
   - game feel, juice, feedback sequences -> `godot-feel`
   - UI navigation, screen flow, transitions -> `godot-common-ui`
   - camera rigs, follow, shake -> `godot-camera`
   - structured logging -> `godot-logger`
   - procedural UI shapes -> `godot-procedural-ui`
   - physics bodies, collision architecture, joints, ragdolls, interpolation, movement controllers, Jolt configuration -> `godot-physics`
   - unit/integration testing -> `godot-gdunit4`
6. Hand off the approved decision set to `godot-orchestrator`.

## Output

Produce an architecture decision summary containing:

- problem statement
- chosen approach
- compatibility constraints preserved
- execution-surface constraints preserved
- rejected alternatives
- engine-specific rationale
- risk list
- required follow-on skills

For complex features (multiple interacting components, non-trivial algorithms, or backward compatibility constraints), the decision summary should also include:

- **component interfaces**: public API signatures (signals, methods, exported properties) for each new or modified component. Keep these to the contract level: what callers see, not internal implementation. This prevents the orchestrator and implementation skills from inventing incompatible interfaces independently.
- **key algorithm pseudocode**: when the feature contains non-trivial logic (state machines, caching strategies, fallback chains, interpolation schemes), include GDScript-style pseudocode for those algorithms. Do not pseudocode simple CRUD or straightforward signal wiring. The threshold is: if two developers could reasonably implement it differently in ways that break integration, write the pseudocode.
- **error classification**: for each identified failure mode, classify it using the foundation error categories (A: programmer error/crash, B: data error/visible degradation, C: runtime condition/handle). Only list errors that are non-obvious. Do not enumerate every possible null check.
- **correctness properties**: formal invariants that must hold across all valid executions. Write these as "for any X, Y shall hold" statements. These become the basis for test assertions in the orchestrator's task plan. Only include properties for behaviors where violations would be subtle or hard to catch by inspection.

The decision about whether to include these sections is based on feature complexity, not a fixed rule. A simple "add a dash ability" feature needs none of them. A "scene transition system with caching, shader fallback, and backward compatibility" needs all of them.

## Quality Gates

- Decisions are tied to Godot behavior, not generic game-dev advice.
- The chosen approach fits the current project rather than replacing it with a theoretical greenfield design.
- The summary makes the file-edit versus MCP-editor split explicit when that boundary affects execution.
- Tradeoffs are explicit.
- The summary is concrete enough for task decomposition.
- It does not drift into full implementation code (pseudocode for key algorithms is fine; line-by-line implementation is not).
- When component interfaces are included, they define the contract (signals, public methods, exports) without dictating internal structure.
- When error scenarios are classified, they use the foundation categories (A/B/C) so implementation skills know whether to crash, degrade visibly, or handle.
- When correctness properties are included, they are testable assertions, not vague aspirations.

## Failure Modes

- Do not create a giant design document. Even with the expanded sections, keep the total output under 300 lines for complex features. Simple features should be much shorter.
- Do not recommend inheritance-heavy or globally coupled patterns by default.
- Do not escalate to C# or GDExtension without a proven reason.
- Do not ignore benchmark-backed findings when challenging common myths.
- Do not choose a clean-looking architecture that ignores the current project's actual constraints.
- Do not write pseudocode for trivial logic. The threshold is: would two developers implement this differently in ways that break integration?
- Do not add graceful recovery to error scenarios unless the design explicitly requires degraded operation. Default to fail-fast (Category A).

## References

Read only as needed:

- `references/decision-lenses.md`
- `references/examples-and-validation.md`
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/Godot Language Strategy Guide.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
- `../../foundation/async_loading_streaming_godot4_report.md`
