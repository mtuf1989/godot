---
name: godot-scope
description: |
  Scope large Godot game requests into a realistic implementation slice before architecture or coding.
  Use when the user shares a game idea, game concept, game design document (GDD), feature list, product requirements, or any broad multi-feature request that is likely too large for one build phase.
  Also use when someone says "I want to make a game," "where do I start," "help me plan this," "what should I build first," or provides multiple design documents that need prioritization.
  If the request mentions more than one major system (e.g., combat + crafting + multiplayer), this skill should fire before any architecture or coding work begins.
---

# Godot Scope

Reduce a game request or existing-project ask into a bounded slice another agent can architect and implement reliably.

Read `references/session-scope-heuristics.md` first. Read `references/examples-and-validation.md` before producing the final scoped brief.

## Responsibility

- Distill game ideas, GDDs, PRDs, feature lists, and research notes into a realistic next slice.
- Preserve product intent while cutting scope to what can be delivered and validated cleanly.
- Preserve the current project's non-negotiable constraints when the task extends an existing game.
- Produce a scoped brief or `SESSION_PLAN.md` style artifact for the next phase.

## Use When

- The user provides many design documents or a large concept.
- The request mixes core loop, polish, progression, UI, multiplayer, or content expansion.
- The likely implementation would exceed a single coherent feature slice.

## Do Not Use When

- The feature is already narrow, approved, and implementation-ready.
- The main problem is choosing engine patterns or language/runtime tradeoffs.
- The task is specifically about save/load, scene/resource behavior, or task decomposition.

## Workflow

1. Read `references/session-scope-heuristics.md`. If the task extends an existing project, inspect the current project facts that constrain the slice. Then read all user-provided design materials and extract:
   - core loop
   - player actions
   - essential entities
   - required screens/scenes
   - project constraints to preserve:
     - 2D vs 3D
     - target platforms
     - input modes
     - camera or presentation assumptions
     - systems that must remain intact
   - systems that can be deferred
2. Classify each feature:
   - `must now`
   - `later`
   - `out of scope for current slice`
3. Define one bounded slice with:
   - playable objective
   - max feature count
   - minimal required assets/content
   - preserved project constraints
   - validation scenario
4. Read `references/examples-and-validation.md` and write the scoped brief with concrete deliverables.
5. Hand off to `godot-architect`.

## Output

Produce a concise scoped brief or `SESSION_PLAN.md` equivalent containing:

- slice title
- source inputs reviewed
- core gameplay loop for this slice
- project constraints to preserve
- features in scope now
- features explicitly deferred
- assumptions that still need confirmation
- success criteria (concrete, testable statements the architect can trace tasks back to)
- complexity hint: simple (single system, no integration contracts needed) or complex (multiple interacting systems, backward compatibility, non-trivial algorithms). This tells the architect whether to produce the extended output sections.
- recommended next skill: `godot-architect`

## Quality Gates

- The slice is small enough to implement without hidden subsystem expansion.
- Deferred features are named explicitly, not implied.
- The brief describes one coherent playable or testable increment.
- Preserved project constraints are explicit when the request extends an existing project.
- Success criteria are concrete and testable, not vague ("player can X" not "game works").
- Complexity hint is present so the architect knows whether to produce extended output sections.
- Another agent can continue without re-reading every source document first or rediscovering project assumptions.

## Failure Modes

- Do not turn scoping into architecture design.
- Do not promise full-game delivery when the request is obviously multi-phase.
- Do not keep optional systems in scope “just in case”.
- Do not hide cuts; record deferrals explicitly.
- Do not ignore the existing project shape when the user is extending one.

## References

Read only as needed:

- `references/session-scope-heuristics.md`
- `references/examples-and-validation.md`
- `../../foundation/Godot Nuanced Development Practices.md`
