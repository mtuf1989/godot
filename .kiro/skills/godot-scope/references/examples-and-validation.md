# Examples and Validation

Use this file after applying the core heuristics and before finalizing output.

## Example Input

User request:

"Build a top-down roguelite with 4 classes, relics, base upgrades, online co-op, daily missions, crafting, and boss raids."

## Good Output Shape

```md
# Scoped Slice: Arena Survival Prototype

## Inputs Reviewed
- user prompt

## Core Loop
- Move through one arena, avoid enemies, attack, survive waves, and end on win/lose.

## Project Constraints To Preserve
- 2D top-down presentation
- keyboard and mouse input
- no persistence in this first slice

## In Scope Now
- one player class
- one arena scene
- two enemy types
- one win/lose loop
- minimal HUD

## Deferred
- co-op -> new networking domain
- daily missions -> meta loop
- crafting -> new economy subsystem
- base upgrades -> persistent progression
- boss raids -> later content tier

## Success Criteria
- player can move and attack in the arena
- enemies spawn in waves with increasing difficulty
- game ends with a clear win or lose state
- HUD shows health and wave number

## Complexity Hint
- simple
- single gameplay system, no integration contracts or backward compatibility

## Next Skill
- godot-architect
```

## Validation Checklist

- names one coherent slice, not a vague phase
- preserves current project constraints when applicable
- states what is deferred and why
- includes concrete, testable success criteria (not just "it works")
- includes a complexity hint so the architect knows whether to produce extended sections
- does not choose node/resource/language architecture
- is small enough for the next skill to reason about concretely

## Bad Signs

- output still includes multiplayer, progression, and content expansion in the first slice
- output ignores obvious current-project constraints
- output contains architecture advice instead of scope boundaries
- deferred items are omitted

## Escalate

Ask the user only if:

- two incompatible game concepts are mixed together
- no core loop can be identified
- the user explicitly refuses any deferral
