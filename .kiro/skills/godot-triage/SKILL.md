---
name: godot-triage
description: |
  Classify any Godot request by complexity and route it to the correct pipeline entry point.
  This skill should fire FIRST on every Godot request before any other godot skill.
  Use when the user asks for anything Godot-related: a new feature, a bug fix, a refactor, a question about architecture, a full game idea, or a one-line tweak.
  Also use when someone says "fix this," "change this," "add this," "build this," "I want to make," "help me with," or provides any Godot task regardless of size.
  If you are unsure whether a Godot request needs the full scope-architect-orchestrator pipeline or can skip straight to implementation, this skill decides.
---

# Godot Triage

Classify the incoming request and route it to the right skill. Do not do the work — just decide where it starts.

## Responsibility

- Read the request and determine its complexity tier.
- Route to the correct entry point in the skill pipeline.
- Prevent small requests from paying the cost of the full pipeline.
- Prevent large requests from skipping necessary planning.

## Use When

- Any Godot-related request arrives.
- This is always the first skill to consider.

## Do Not Use When

- The request is not Godot-related.
- A previous triage decision already routed this request and work is in progress.

## Complexity Tiers

### Tier 1 — Direct Fix

The request targets a specific, known behavior in existing code. The change is localized to one function, one script, or one scene property.

Examples:
- "Change enemy spawn from row to random angles"
- "Fix the player clipping through walls"
- "Increase jump height to 400"
- "The score label isn't updating"
- "Swap the dash input from Shift to Space"

Signals:
- The user names a specific file, node, or behavior
- The fix is obvious without architectural analysis
- No new systems, scenes, or resources are introduced
- Estimated change: under ~50 lines in 1-2 files

Route: **godot-gdscript** directly.

### Tier 2 — Small Feature

The request adds a contained piece of functionality that touches a few files but doesn't require scope reduction or deep architectural decisions. The pattern to use is either obvious or has only one reasonable choice.

Examples:
- "Add a pause menu"
- "Make enemies drop health pickups"
- "Add screen shake on hit"
- "Implement a simple countdown timer"
- "Add a death animation for the player"
- "Add simple patrol behavior to this enemy"
- "Make the slime chase the player when nearby"

Signals:
- One new behavior or UI element
- May need a new scene or script, but the structure is straightforward
- No competing architectural approaches worth debating
- Estimated change: 1-3 files, under ~200 lines total

Route: **godot-architect** (quick pattern check) → **godot-gdscript**.
Skip orchestrator — the task list is trivially obvious.

When the task clearly belongs to an addon-specific skill, route directly to that skill (with a quick architect check if the pattern choice is non-obvious):
- ANY UI screen, popup, menu, HUD, dialog, loading screen, level/world transition, screen flow, or navigation wiring → **godot-common-ui** (this is the default UI workflow; only use godot-ui-core for pure layout/anchors/containers/styling with zero navigation)
- Behavior trees / HSM / NPC AI → **godot-limboai**
- Camera follow, dead zones, rig setup, screen shake via camera modules → **godot-camera**
- Game juice, hit pause, scale pops, feedback sequences → **godot-feel**
- Sound effects, music, crossfade, ambient audio → **godot-sound**
- Dialogue trees, NPC conversations, balloon scenes → **godot-dialogue**
- 2D particles, trails, screen-space VFX → **godot-vfx-2d**
- 3D particles, explosions, volumetric effects → **godot-vfx**
- 2D shader effects (hit flash, dissolve, outline, palette swap) → **godot-shader-canvasitem-fx**
- 3D spatial shaders or compositor post-processing → **godot-shader-spatial**
- Procedural UI shapes (rounded buttons, cards, SDF panels) → **godot-procedural-ui**
- GLog event logging, session instrumentation → **godot-logger**
- Placeholder 2D prototype art → **godot-prototype-assets-2d**
- 3D blockout / greybox level geometry → **godot-blockout-3d**
- unit tests, integration tests → **godot-gdunit4**
- Animation systems (AnimationPlayer, AnimationTree, sprite animation, procedural animation, attack combos, IK) → **godot-animation**
- Physics bodies, collision layers, raycasting, joints, ragdolls, interpolation, movement controllers, hitbox/hurtbox → **godot-physics**

### Tier 3 — Multi-System Feature

The request involves multiple interacting systems, new scene structures, or design decisions with meaningful tradeoffs. Scope may need reduction.

Examples:
- "Add an inventory system with crafting"
- "Implement multiplayer co-op"
- "Build a dialogue system with branching and save/load"
- "Create a level editor"
- "Add a skill tree with unlockable abilities"
- "Build a boss AI with phase transitions and squad coordination"

Signals:
- Multiple new scenes, scripts, or resources
- Architectural tradeoffs exist (composition vs inheritance, data-driven vs node-based, etc.)
- The request implies subsystems that could expand in scope
- Dependencies between new components need sequencing

Route: **godot-scope** → **godot-architect** → **godot-orchestrator** → implementation skills.
Full pipeline.

### Tier 4 — Full Game or Major Rework

The request is a complete game concept, a GDD, or a fundamental restructuring of an existing project.

Examples:
- "I want to make a roguelite with classes and co-op"
- "Here's my game design document"
- "Rewrite the project from 2D to 3D"
- "Port the game to multiplayer"

Signals:
- No existing bounded scope
- Multiple major systems implied
- The core loop itself needs definition or redefinition

Route: **godot-scope** → full pipeline.

## Workflow

1. Read the user's request.
2. If the request extends an existing project, note what project context is available (existing files, scenes, scripts mentioned or open).
3. Match the request against the tier signals above. Use the first tier that fits — don't over-classify.
4. State the tier and the routing decision in one or two sentences.
5. Hand off to the routed skill immediately.

## Ambiguity Rules

- When in doubt between Tier 1 and Tier 2, pick Tier 1. You can always escalate if the gdscript skill hits something unexpected.
- When in doubt between Tier 2 and Tier 3, pick Tier 2. The architect skill will escalate to scope if it discovers hidden complexity.
- When in doubt between Tier 3 and Tier 4, pick Tier 3. Scope will catch if further reduction is needed.
- Bias toward the lighter tier. The pipeline has escalation paths built in — every skill can route upward if it discovers the request is bigger than expected.
- **AI / behavior tree / HSM requests**: If the project uses LimboAI (or the request mentions behavior trees, BTPlayer, LimboHSM, blackboard, or NPC/enemy AI), route implementation to **godot-limboai** instead of **godot-gdscript**.
- **Addon-specific requests at Tier 2**: When the request clearly maps to an addon-specific skill (see the Tier 2 routing list), route to that skill directly. The addon skills have their own escalation paths if the task turns out to be more complex than expected.

## Output

One short routing statement:

```
Tier <N>: <one-line rationale>
Route: <skill> [→ <skill> → ...]
```

Regardless of tier or how thorough existing architecture docs are, the executor's precondition checklist (`godot-gdscript/references/executor-checklist.md`) is non-optional. Good docs reduce design risk but do not eliminate mechanical coding errors — the checklist catches those.

## Quality Gates

- The routing decision is made in seconds, not minutes.
- Small requests never touch the orchestrator.
- Large requests always start at scope.
- The rationale is concrete enough that the user can disagree and override.

## Failure Modes

- Do not turn triage into scope reduction. If you're writing a "Deferred" list, you've gone too far.
- Do not turn triage into architecture. If you're comparing patterns, you've gone too far.
- Do not default to the full pipeline out of caution. Bias lighter.
- Do not skip triage for "obvious" large requests — even Tier 4 benefits from the explicit routing statement.
