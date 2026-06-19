# Session Scope Heuristics

Use this file before reading large design docs in detail.

## Default Goal

Reduce the current request to one slice that is:

- coherent
- buildable without hidden subsystem expansion
- testable with one clear validation scenario

## Slice Sizing Defaults

Aim for one slice with no more than:

- 1 core gameplay loop
- 1 main playable scene or screen
- 3 to 5 essential entity types
- 1 progression layer at most
- 1 validation scenario

Cut first:

- extra modes
- meta progression
- social features
- economy complexity
- content breadth
- polish-only asks that do not change the loop

## Core Loop Extraction

Extract and name these items:

- player does what?
- against what?
- to achieve what?
- how does success/failure show up?

If any answer is unclear, the scope is still too fuzzy for architecture.

## Constraint Capture

If the request modifies an existing project, capture:

- 2D vs 3D baseline
- current input assumptions
- target platforms already implied by the project
- camera or presentation constraints
- systems the user expects to keep working

Do not let later skills rediscover these from scratch.

## Deferral Rules

Defer by default when a feature:

- requires another screen to justify itself
- introduces a new persistence or networking concern
- needs bespoke content production to validate the loop
- does not change the minimum playable test

## Output Template

```md
# Scoped Slice: <title>

## Inputs Reviewed
- <doc or source>

## Core Loop
- <one paragraph>

## Project Constraints To Preserve
- <constraint>

## In Scope Now
- <feature>

## Deferred
- <feature> -> later because <reason>

## Assumptions Requiring Confirmation
- <assumption> or none

## Success Criteria
- <concrete testable statement that the architect and orchestrator can trace tasks back to>
- <another criterion>

## Complexity Hint
- simple | complex
- <one sentence justification: e.g., "single system, no integration contracts" or "three interacting systems with backward compatibility constraints">

## Next Skill
- godot-architect
```

## Example

Input: "Top-down roguelite with classes, relics, crafting, online co-op, and seasonal events."

Good first slice:

- one arena
- one player class
- one enemy family
- survive or clear wave objective
- no crafting, no co-op, no events

If extending an existing 2D keyboard-and-mouse project, preserve that unless the user explicitly approves a platform or input change.

## Escalate

Stop and ask for clarification only if:

- there is no identifiable core loop
- two different game concepts are mixed together
- the user insists all deferred systems are mandatory now
- the existing project constraints directly conflict with the requested slice
