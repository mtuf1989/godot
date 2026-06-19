---
name: godot-ggas
description: |
  Implement gameplay abilities, effects, attributes, and tag logic using the GGAS (Godot Gameplay Ability System) plugin.
  Use when the task involves AbilitySystemComponent, GameplayAbilityResource, GameplayEffectResource, AttributeSetResource, GameplayTagResource, AbilityTask, GameplayCue, CustomExecutionCalculation, or any GGAS API.
  Also use when someone says "create an ability," "add a gameplay effect," "set up attributes," "define tags," "implement a buff/debuff," "damage calculation," "cooldown system," "ability cost," "stacking effects," "tag requirements," "immunity," "periodic tick," "DoT," "HoT," "preview damage," "modifier preview," or any combat/RPG mechanic that maps to the GAS pattern.
  If the user needs to wire up, configure, or extend any part of the GGAS plugin from GDScript, this skill should fire.
---

# Godot GGAS

Implement gameplay abilities, effects, attributes, and tag-driven logic using the GGAS plugin's dual-layer architecture: C++ core for performance, GDScript resources for designer iteration.

Read `references/api-patterns.md` first. Read `references/validation-checklist.md` before finalizing any implementation.

## Responsibility

- Inspect the project's existing GGAS setup before editing:
  - which `AbilitySystemComponent` nodes exist and their current configuration
  - existing attribute sets, tag hierarchies, effects, and abilities
  - how the ASC signals are already connected to other systems
  - the project's tag naming convention
- Create or modify GGAS resources and scripts:
  - `GameplayAbilityResource` definitions with activation requirements
  - `GameplayEffectResource` with modifiers, duration, stacking, and periodic tick
  - `AttributeSetResource` with appropriate attribute design
  - `GameplayTagResource` hierarchies
  - `CustomExecutionCalculation` subclasses for complex formulas
  - `AbilityTask` subclasses for async ability flows
  - `GameplayCue` triggers for feedback hooks
- Wire `AbilitySystemComponent` into scenes with proper owner/avatar setup.
- Connect ASC signals to game systems (UI, AI, persistence).
- Design tag taxonomies that support activation, blocking, immunity, granted lifecycle, and removal logic.

## Use When

- The task involves creating, modifying, or debugging any GGAS resource or script.
- The user needs abilities with costs, cooldowns, tag requirements, or async flows.
- The user needs lightweight instant abilities via `execute_ability()`.
- The user needs effects that modify attributes with duration, stacking, periodic tick, or custom calculations.
- The user needs a tag hierarchy for state management, immunity, or conditional logic.
- The user needs to wire an `AbilitySystemComponent` into a character or entity.
- The user needs gameplay cues triggered by effects or abilities.
- The user needs to preview effect impact on attributes without applying them.
- The user needs to query or remove active effects by tag or source.

## Do Not Use When

- The task is deciding *whether* to use GGAS vs a simpler approach — route to `godot-architect`.
- The task is general GDScript that doesn't touch GGAS APIs — route to `godot-gdscript`.
- The task modifies the C++ source of GGAS itself — route to `godot-gdextension-cpp`.
- The task is purely visual/audio feedback implementation beyond cue triggers — route to `godot-vfx-2d` / `godot-vfx` for VFX, `godot-sound` for audio.
- The task is animation system work beyond `play_montage_and_wait` — route to `godot-animation`.
- The task is AI behavior that *uses* abilities but the logic is behavior-tree-driven — route to `godot-limboai` (which will call back to godot-ggas for the ability wiring).

## Workflow

1. Read `references/api-patterns.md`, then inspect the project's current GGAS state:
   - existing `AbilitySystemComponent` nodes in scenes
   - existing resources in the project's ability/effect/attribute directories
   - tag naming conventions already in use
   - how ASC signals are connected
   - whether the project uses the `framework/` extensibility layer

2. Determine what GGAS primitives the task requires:
   - **Ability** — a discrete action the entity can perform (fireball, dash, heal)
   - **Effect** — a data-driven modifier applied to attributes (damage, buff, DoT, HoT)
   - **Attribute** — a numeric value that effects modify (health, mana, speed)
   - **Tag** — a hierarchical label for state and requirements (Status.Debuff.Burning)
   - **Task** — an async step within an ability (wait for target, play animation)
   - **Cue** — a feedback hook triggered by effects/abilities (VFX, SFX, screen shake)
   - **Custom Calculation** — complex formulas beyond simple add/multiply (including tick calcs)

3. Design the tag taxonomy for the feature:
   - Use dot-separated hierarchy: `Category.Subcategory.Specific`
   - Plan required_tags, blocked_tags, immunity_tags, granted_tags, removal triggers
   - Ensure tags compose correctly with existing tag hierarchies
   - Remember: `has_tag("Status.Debuff")` matches `Status.Debuff.Burning` hierarchically
   - Remember: granted_tags are ref-counted — two effects granting the same tag won't conflict on removal

4. Implement resources and scripts following these patterns:
   - One resource file per ability/effect (not monolithic configs)
   - Use `GameplayEffectModifier` for simple math; `CustomExecutionCalculation` for complex formulas
   - Use `AbilityTask` subclasses for async flows; chain with `await`
   - Set `evaluation_channel` when multiple modifiers must apply in a specific order
   - Multiply modifiers are bias-corrected: pass `1.5` for +50%, `1.3` for +30%, `0.5` for -50%
   - For DoT/HoT, configure `tick_interval` on the effect rather than building external timers
   - For simple instant abilities, prefer `execute_ability()` over the full async flow

5. Wire the ASC into the scene:
   - Add `AbilitySystemComponent` as a child of the entity
   - Set `owner_actor` (the entity that "owns" the abilities)
   - Set `avatar_actor` (the physical representation, may differ for summons)
   - Assign the `AttributeSet` via `set_attribute_set_from_resource(resource)` (auto-unwraps GDScript wrappers)
   - Grant initial abilities via `grant_ability()`
   - Connect signals for UI/feedback: `attribute_changed`, `ability_activated`, `effect_applied`, `tag_added`, `effect_ticked`

6. Read `references/validation-checklist.md` and validate:
   - Tag requirements are satisfiable (no impossible activation conditions)
   - Effect modifiers target attributes that exist in the assigned AttributeSet
   - Cooldown tags are unique per ability (avoid shared cooldown collisions unless intentional)
   - Stacking config makes sense (max_stack_count > 0, refresh_duration_on_stack matches design intent)
   - Custom calculations handle edge cases (zero resistance, missing attributes)
   - Tick intervals are reasonable (not too fast for the effect's purpose)
   - Ability costs reference existing attributes with correct amounts

## Key Concepts

### Duration Policies
- **Instant** — applies once, modifies base values directly, then gone
- **Infinite** — persists until explicitly removed (buffs, auras, equipment stats)
- **Has_Duration** — persists for a set time, then auto-removes (DoTs, timed buffs)

### Modifier Operations
- **Add** — `base + magnitude`
- **Multiply** — bias-corrected: pass `1.5` for +50%. Formula: `base × (1 + sum_of(magnitude - 1))`. Two `1.5` modifiers = `base × 2.0`, not `base × 2.25`
- **Divide** — `base / magnitude`
- **Override** — replaces value entirely (ignores base and other modifiers)

### Magnitude Calculation Types
- **Scalable_Float** — fixed value, optionally scaled by level
- **Attribute_Based** — derived from another attribute on source or target
- **Set_By_Caller** — value passed at runtime via activation context

### Two-Tier Ability System

GGAS supports two ability execution paths:

**Tier 1 — Instant (`execute_ability`):** For simple abilities that validate, deduct costs, fire, and end immediately. No async, no tasks. The C++ layer handles the full lifecycle in one call:
1. Validates activation (cooldown, required/blocked tags)
2. Checks and deducts costs
3. Starts cooldown
4. Grants activation tags
5. Emits `ability_activated` signal
6. Auto-ends (removes activation tags, emits `ability_ended`)

**Tier 2 — Async (full flow):** For complex abilities with targeting, animations, multi-step sequences. Uses `AbilityActivationContext` and `AbilityTask` subclasses with `await`.

Choose Tier 1 for the 80% case (attacks, instant heals, toggles). Choose Tier 2 when you need targeting input, animation waits, or multi-step sequences.

### Periodic Tick System

Duration effects can execute periodic logic via `tick_interval`:
- `tick_interval` — seconds between ticks (0 = no tick)
- `tick_on_application` — whether to fire the first tick immediately on apply
- `tick_execution_calculation_class` — optional separate calc class for tick logic (uses main exec calc if empty)

Ticks fire the `effect_ticked(effect_id, stack_count, tick_number)` signal. Tick damage/healing scales with `stack_count`. The ASC only processes ticking effects (optimized — doesn't iterate non-ticking effects every frame).

Use `process_effects(delta)` to manually advance tick processing (useful for testing or pause-aware systems).

### Granted Tag Lifecycle

Effects with `granted_tags` automatically add those tags to the target on application and remove them on expiry/removal. Tags are **ref-counted**: if two effects grant the same tag, the tag persists until both effects are removed. This eliminates manual tag management for status effects.

### Attribute Clamping

AttributeSet supports optional clamp rules to bound attribute values without manual signal wiring:
- `set_clamp_rule(attr, min, max)` — fixed bounds (e.g., health min 0)
- `set_clamp_rule_dynamic(attr, min_attr, max_attr)` — bounds from other attributes (e.g., health capped by max_health)
- Clamping applies after modifier aggregation, before `attribute_changed` signal fires
- Configure via `clamp_rules` dictionary on `AttributeSetResource`:
  ```
  clamp_rules = {"health": {"min_value": 0.0, "max_attribute": "max_health"}}
  ```

### Modifier Preview

For UI tooltips or "what-if" calculations:
- `preview_attribute_value(attribute_name, hypothetical_modifiers)` — returns what the value would be with additional modifiers
- `preview_effect_on_attribute(effect, attribute_name, level)` — returns what applying this effect would do to the attribute

These are read-only — no state is mutated. For effects with custom execution calculations, override `_preview_calculation(spec, target, level) -> Dictionary` in your calc class to return `{"attribute_name": delta_value}`. If not overridden, the preview returns the current value (no change shown).

### The Dual API Surface
The C++ core exposes two tiers for every operation:
- `method_name()` — GDScript-safe, validated, emits signals. **Always use this from GDScript.**
- `method_name_fast()` — C++ internal only, skips validation. Never call from GDScript.

### Ability Lifecycle
```
grant_ability() → can_activate_ability() → try_activate_ability() or execute_ability()
  → [Tier 1: validate → deduct costs → activate → end]
  → [Tier 2: activation context created → tasks execute → apply effects → end_ability()]
```

### Effect Application Methods

Three ways to apply effects, from simplest to most complex:

| Method | Use Case |
|--------|----------|
| `apply_effect(resource)` | Pass a preloaded `GameplayEffectResource` directly (type-safe, rename-safe) |
| `apply_gameplay_effect_resource(cpp_effect, level, source)` | Pass the unwrapped C++ `GameplayEffect` |
| `apply_gameplay_effect(id, duration, stacks, modifiers, source)` | Inline effect with ad-hoc modifiers |

Prefer `apply_effect(resource)` for all new code — it accepts either a C++ `GameplayEffect` or a GDScript `GameplayEffectResource` and auto-unwraps.

### Effect Query & Removal

| Method | Purpose |
|--------|---------|
| `get_active_effects_detailed()` | Full info: tags, source, duration, stacks |
| `get_active_effects_with_tag(tag)` | Effects whose `granted_tags` contain this tag |
| `get_effect_stack_count_by_tag(tag)` | Sum of stacks across matching effects |
| `get_effect_remaining_duration_by_tag(tag)` | Duration of first matching effect |
| `remove_effects_with_tag(tag)` | Remove all effects granting this tag |
| `remove_effect_by_id(effect_id)` | Remove a specific effect instance |
| `remove_all_effects_from_source(source_asc)` | Remove all effects from a source (e.g., on source death) |

### ⚠️ Non-Obvious Signatures — Read Before Writing

These APIs have signatures that differ from what you'd guess. Do not infer them — read `references/api-patterns.md` at the linked section before writing any of these:

| Extension Point | What's Non-Obvious | Reference Section |
|----------------|-------------------|-------------------|
| `CustomExecutionCalculation` | `apply_modifier_to_target(target, attr, op, magnitude)` — target is explicit 1st param, not implicit. `get_source_attribute_value(spec, attr)` takes `spec` not `self`. | `#custom-execution-calculations` |
| `AbilityTask` subclass | Override `_start_task_implementation()`, resolve via `complete_task()`/`cancel_task()`/`fail_task()`. Do NOT override `execute()`. | `#async-ability-flows-with-tasks` |
| Multiply magnitude | Pass `1.5` for +50%, NOT `0.5`. The system extracts `(magnitude - 1.0)` as bonus. `0.5` means **-50%**. | `#defining-effects` |
| `apply_effect()` source param | 3rd parameter is source ASC — omitting it makes `get_source_attribute_value()` return 0 inside custom calcs. | `#direct-effect-application` |
| `_preview_calculation` | Method name uses underscore prefix (Godot virtual convention). Returns `Dictionary` of `{attr: delta}`, not absolute values. | `#modifier-preview` |
| `GameplayEffectResource` properties | `duration_magnitude` (not `duration`). `granted_tags` is `Array[GameplayTagResource]` — access tag name via `.tag_name`. `modifiers` contains `GameplayEffectModifier` with `attribute_name` (not `attribute`), `operation`, `magnitude_value`. `max_stack_count` for stacking limit. | Read the `.tres` file or resource script if unsure. |

## Output

Produce implementation work that includes:
- Resources created/modified (ability, effect, attribute set, tags)
- Scripts created/modified (custom calculations, custom tasks, ability logic)
- Scene wiring (ASC node placement, signal connections)
- Tag taxonomy additions with rationale
- Validation performed and any edge cases flagged

## Quality Gates

- Every ability has a clear activation path (required tags achievable, costs payable).
- Every effect targets attributes that exist in the target's AttributeSet.
- Tag hierarchies are consistent with existing project conventions.
- Custom calculations handle missing/zero values gracefully.
- No `_fast()` methods called from GDScript.
- Stacking and duration configs match the design intent.
- Tick intervals are appropriate for the game's frame rate and design (avoid < 0.1s unless needed).
- Cue triggers are named but VFX/audio implementation deferred to `godot-vfx-2d` / `godot-vfx` / `godot-sound` unless trivial.
- The implementation uses the GDScript resource layer, not raw C++ API calls, unless there's a justified performance reason.
- `apply_effect()` is preferred over `apply_gameplay_effect_resource()` for cleaner code.
- `set_attribute_set_from_resource()` is used instead of manual `get_cpp_attribute_set()` unwrapping.
- Source ASC is always passed when effects need source attribution for calculations.

## Failure Modes

- Do not create abilities without considering their tag requirements against the entity's achievable state.
- Do not use Override modifiers unless the design explicitly requires ignoring all other modifiers.
- Do not put complex game logic inside CustomExecutionCalculation — keep it to math. Game logic belongs in the ability script.
- Do not manually manage effect lifetimes for Has_Duration effects — the ASC handles expiration.
- Do not create circular tag dependencies (effect grants tag that blocks its own application).
- Do not skip the AttributeSet validation — effects targeting nonexistent attributes fail silently.
- Do not build external timers for periodic damage/healing — use the tick system instead.
- Do not manually add/remove tags that effects should manage via `granted_tags` — let the ref-counted lifecycle handle it.
- Do not rely on `preview_effect_on_attribute()` for effects using custom execution calculations unless they override `_preview_calculation()`.
- Do not manually clamp attributes via `attribute_changed` signals if `clamp_rules` is configured — it will double-clamp.

## Boundary Skills

When the task crosses into another domain, hand off explicitly:

| Domain | Route To | Bridge Point |
|--------|----------|--------------|
| Architecture decisions | `godot-architect` | Before GGAS work begins |
| C++ core modifications | `godot-gdextension-cpp` | When GDScript layer is insufficient |
| Visual/audio feedback | `godot-vfx-2d` / `godot-vfx` / `godot-sound` | After cue trigger is defined |
| Animation playback | `godot-animation` | `play_montage_and_wait` task |
| AI using abilities | `godot-limboai` | BT action calls `execute_ability()` or `try_activate_ability()` |
| UI displaying attributes | `godot-common-ui` | ASC `attribute_changed` signal |
| Persistence of ability state | `godot-persistence` | Save/load granted abilities + active effects |

## References

Read only as needed:

- `references/api-patterns.md` — common implementation patterns with code examples
- `references/tres-file-reference.md` — examples of .tres file resources for ggas
- `references/validation-checklist.md` — pre-delivery validation steps
- `../../foundation/Godot Nuanced Development Practices.md` — engine conventions
