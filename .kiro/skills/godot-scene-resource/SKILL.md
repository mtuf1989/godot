---
name: godot-scene-resource
description: |
  Handle Godot scene composition, resource ownership, sharing, duplication, and instancing safely.
  Use when tasks touch scenes, Resources, PackedScene instancing, mutable subresources, or ownership semantics.
  Also use when someone encounters shared state bugs between scene instances, asks about resource_local_to_scene, duplicate(), built-in resources leaking across instances, or wonders why changing one enemy's data affects all enemies.
  If the work involves spawning multiple instances of the same scene, attaching mutable Resources to entities, or deciding whether data belongs on nodes vs resources vs registries, this skill should fire.
---

# Godot Scene Resource

Prevent hidden shared-state bugs and scene-structure mistakes before they reach implementation.

Read `references/resource-decision-table.md` first. Read `references/examples-and-validation.md` before writing the final note.

## Responsibility

- Recommend safe scene and resource patterns.
- Identify when data should live on nodes, resources, or separate registries.
- Guard against Godot-specific sharing, duplication, and ownership pitfalls.
- Tie the recommendation to the concrete scenes, resources, and scripts in the current project.
- When editor-connected, use `open_scene` plus `scene://current/tree` if the live scene hierarchy or owner relationships matter to the recommendation.

## Use When

- A feature instantiates scenes repeatedly.
- Mutable `Resource` objects are attached to entities or scenes.
- The design involves built-in resources, duplicated nodes, or per-instance state.
- The work depends on `resource_local_to_scene`, `duplicate()`, or `PackedScene`.

## Do Not Use When

- The task is generic gameplay logic without scene/resource risk.
- The problem is primarily save/load schema design.
- The next step is coding syntax, not scene/resource behavior.

## Workflow

1. Read `references/resource-decision-table.md`, then inspect the concrete scenes, resources, and scripts involved. In editor-connected mode, use `open_scene` plus `scene://current/tree` when the live owner hierarchy or node placement matters to the decision, or `find_nodes` for targeted lookups of nodes that share resources or have specific ownership patterns. Identify the state that must be shared vs isolated.
2. Decide where that state should live:
   - node properties for per-instance mutable state
   - resources for shared or serialized data
   - explicit duplication when mutation isolation is required
3. Check for known Godot pitfalls:
   - resource cache reuse
   - built-in resource sharing across scene instances
   - `resource_local_to_scene` edge cases
   - shallow vs deep duplication mismatch
   - MCP `duplicate_node` handles ownership correctly (sets owner to scene root for the duplicate and all descendants), but duplicated resources are still shared references unless `resource_local_to_scene` is enabled
4. Read `references/examples-and-validation.md`, then recommend the safe pattern and the validation step against the actual project artifacts.
5. If persistence is involved, route identity concerns to `godot-persistence`.

## Output

Produce a scene/resource design note containing:

- affected artifacts
- grounding method used if editor-connected
- what is shared
- what is unique per instance
- recommended ownership model
- duplication/local-to-scene decision
- validation checks to confirm no state leakage

## Quality Gates

- Shared vs unique state is explicit.
- Recommendations account for Godot caching and instancing behavior.
- The note includes a concrete verification step for leakage or duplication bugs.
- The note is grounded in actual scenes/resources instead of hypothetical examples.
- Editor-connected hierarchy claims are based on a real `scene://current/tree` read when that structure matters.

## Failure Modes

- Do not treat `Resource` objects as safe mutable per-instance state by default.
- Do not assume `resource_local_to_scene` always works in complex graphs.
- Do not rely on node duplication alone for resource isolation.
- Do not use deep duplication casually on large graphs without intent.
- Do not issue abstract advice without naming the concrete owner scene/resource involved.

## References

Read only as needed:

- `references/resource-decision-table.md`
- `references/examples-and-validation.md`
- `../../foundation/Research_Resource.md`
- `../../foundation/Godot Nuanced Development Practices.md`
- `../../foundation/benchmark_driven_performance_methodology.md`
