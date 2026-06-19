# GGAS Validation Checklist

Run through these checks before delivering any GGAS implementation. Each section maps to a common failure mode.

---

## 1. Attribute Targeting

- [ ] Every `GameplayEffectModifier.attribute_name` exists in the target's `AttributeSetResource`
- [ ] Attribute names match exactly (case-sensitive StringName comparison)
- [ ] Custom attributes are registered if using names beyond the built-in set
- [ ] Effects targeting "health" don't accidentally target "max_health" or vice versa

**Why:** Effects targeting nonexistent attributes emit a warning and skip the modifier — but the effect still "applies" (tracks duration, grants tags). The warning helps debugging, but the modifier is silently lost.

---

## 2. Tag Achievability

- [ ] `required_tags` on abilities are actually achievable by the entity in normal gameplay
- [ ] `blocked_tags` don't create permanent lockouts (e.g., a tag that's never removed)
- [ ] No circular dependencies: effect grants tag X, but tag X blocks the effect's application
- [ ] Cooldown tags are unique per ability unless shared cooldowns are intentional
- [ ] Immunity tags are scoped correctly (don't accidentally block friendly effects)
- [ ] Granted tags from effects don't conflict with manually managed tags (refcount desync)

**Why:** Unreachable activation conditions mean the ability can never fire. Circular tag deps mean effects apply and immediately remove themselves. Mixing manual tag management with effect-granted tags causes refcount issues.

---

## 3. Duration & Lifecycle

- [ ] `Has_Duration` effects have `duration_magnitude > 0`
- [ ] `Infinite` effects have an explicit removal path (ability end, condition, or manual call)
- [ ] `Instant` effects don't set `granted_tags` (they'd be granted and immediately lost)
- [ ] Effects that grant tags clean them up on removal (automatic — but verify no manual `remove_tag()` calls for the same tag)
- [ ] Duration effects with ticks: `duration_magnitude` is long enough for at least one tick to fire

**Why:** Zero-duration effects expire immediately. Infinite effects without removal paths leak permanently. Manual `remove_tag()` on effect-managed tags desyncs the refcount.

---

## 4. Stacking Configuration

- [ ] `max_stack_count > 0` when `allow_stacking = true`
- [ ] `refresh_duration_on_stack` matches design intent:
  - `true` = each new stack resets the timer (forgiving, rewards sustained pressure)
  - `false` = original timer continues (punishes late application)
- [ ] Stacking effects have clear visual feedback per stack (cue or UI)
- [ ] Consider what happens at max stacks: does a new application refresh duration? Get rejected?

**Why:** Stacking with max_stack_count=0 means infinite stacks. Wrong refresh behavior changes game feel dramatically.

---

## 5. Modifier Math

- [ ] Multiply modifiers use bias-corrected values: pass `1.5` for +50%, `0.5` for -50%. The system extracts `(magnitude - 1.0)` as the bonus.
- [ ] Negative Add modifiers for damage (not positive with a "subtract" operation)
- [ ] Override modifiers are used sparingly and intentionally (they ignore all other modifiers)
- [ ] Evaluation channels set when modifier order matters
- [ ] Division modifiers guard against divide-by-zero (magnitude should never be 0)

**Why:** Wrong bias values compound incorrectly. Override without intent silently breaks other systems.

---

## 6. Custom Execution Calculations

- [ ] `can_execute_calculation()` is overridden if the calculation has preconditions
- [ ] `execute_calculation()` handles missing attributes gracefully (returns 0 or skips)
- [ ] Source attribute reads use `get_source_attribute_value()` (not target)
- [ ] Target attribute reads use `get_target_attribute_value()` (not source)
- [ ] Calculations don't contain game logic (spawning, signals, state changes) — only math
- [ ] Resistance/reduction values are clamped to prevent negative damage becoming healing
- [ ] Level scaling doesn't produce absurd values at high levels (test with level 1, 10, 50)

**Why:** Unclamped resistance can invert damage. Game logic in calculations creates hidden side effects that bypass the ability lifecycle.

---

## 7. Periodic Tick Configuration

- [ ] `tick_interval > 0` when periodic behavior is intended
- [ ] `tick_interval` is reasonable for the game's design (not below 0.1s unless needed)
- [ ] `tick_on_application` is intentional: `true` means first tick fires immediately (total ticks = duration/interval + 1)
- [ ] `tick_execution_calculation_class` exists if specified (ClassDB must find it)
- [ ] Tick calc handles the case where source is freed between ticks (ObjectID safety)
- [ ] Duration is long enough for at least one tick: `duration_magnitude >= tick_interval`
- [ ] Effects that only do tick damage have empty `modifiers` array (calc handles it)
- [ ] Consider stack-scaling: tick damage multiplies by `stack_count` — is this intended?

**Why:** Tick interval longer than duration means no ticks fire. Too-fast ticks waste performance. Missing calc class crashes on first tick.

---

## 8. Ability Cost & Cooldown Configuration

- [ ] Costs configured via `set_ability_costs()` reference attributes that exist in the entity's AttributeSet
- [ ] Cost amounts are positive (the system deducts them)
- [ ] Entity can actually afford the cost in normal gameplay (not permanently locked out)
- [ ] Cooldown duration matches design intent (test that ability actually becomes available again)
- [ ] `set_ability_activation_tags()` tags don't conflict with the ability's own `blocked_tags`
- [ ] For `execute_ability()` path: costs and cooldown are configured AFTER `grant_ability()`

**Why:** Costs referencing nonexistent attributes silently pass (deduct from nothing). Activation tags that match blocked_tags create an ability that immediately cancels itself.

---

## 9. Ability Flow

- [ ] Every `try_activate_ability()` path has a matching `end_ability()` or `cancel_ability()`
- [ ] Async tasks check `is_cancelled` after each `await` and exit cleanly
- [ ] `PlayMontageAndWait` has a reasonable timeout (default 30s may be too long)
- [ ] `WaitForTargetData` has a cancel path (player presses escape, timeout)
- [ ] Abilities that apply effects to targets verify the target still exists after async waits
- [ ] Granted tags during activation are cleaned up on cancel (automatic if using the resource system)
- [ ] `execute_ability()` is used for instant abilities (don't use full flow for simple fire-and-forget)

**Why:** Missing end_ability() leaks the "active" state permanently. Unchecked cancellation after await causes null reference crashes.

---

## 10. ASC Wiring

- [ ] `owner_actor` is set (required for source attribute reads in calculations)
- [ ] `avatar_actor` is set (required for physical operations like targeting, animation)
- [ ] `AttributeSet` is assigned via `set_attribute_set_from_resource()` before effects are applied
- [ ] `GameplayCueManager` is assigned if cues are used
- [ ] ASC is a direct child of the entity node (not deeply nested) for easy discovery
- [ ] Signals are connected after ASC initialization, not before
- [ ] Source ASC is passed when applying effects that use custom calculations

**Why:** Missing owner/avatar causes null crashes in calculations. Missing AttributeSet means all attribute reads return 0. Missing source means `get_source_attribute_value()` returns 0.

---

## 11. Tag Naming Conventions

- [ ] Tags follow the project's established hierarchy pattern
- [ ] New tags don't collide with existing ones at the same hierarchy level
- [ ] Hierarchical matching is intentional: adding `Status.Debuff.Burning` means `has_tag("Status.Debuff")` returns true
- [ ] Data tags (Set_By_Caller) use a `Data.` prefix to distinguish from state tags
- [ ] Cooldown tags use `Cooldown.` prefix for clarity

**Why:** Accidental hierarchy collisions cause unexpected tag matches. Inconsistent naming makes the system unmaintainable.

---

## 12. Effect Application & Source Safety

- [ ] `apply_effect()` is preferred over `apply_gameplay_effect_resource()` for new code
- [ ] Source ASC is always passed as 3rd parameter when the effect uses source-dependent calculations
- [ ] Effects applied from entities that can die: game handles source removal gracefully
- [ ] Consider calling `remove_all_effects_from_source(dying_asc)` on target when source dies
- [ ] Never call `_fast()` methods from GDScript (they skip validation, can crash)

**Why:** Missing source attribution means custom calculations read 0 for source stats. Stale source references are safe (ObjectID) but effects may become meaningless without their source.

---

## 13. Preview Usage

- [ ] `preview_attribute_value()` and `preview_effect_on_attribute()` are used read-only for UI
- [ ] Effects with custom calcs override `_preview_calculation()` if preview is needed for UI
- [ ] `_preview_calculation()` returns deltas (not absolutes) and does NOT call `apply_modifier_to_target()`
- [ ] Preview results are not cached long-term (attribute state changes between frames)
- [ ] Set-by-caller magnitudes default to 0 in previews unless explicitly provided

**Why:** Previews are point-in-time snapshots. Calling `apply_modifier_to_target` inside `_preview_calculation` would mutate state.

---

## 14. Performance Considerations

- [ ] Never call `_fast()` methods from GDScript (they skip validation, can crash)
- [ ] Avoid applying/removing effects every frame — batch or use duration effects instead
- [ ] Tag checks in `_process()` use `has_tag()` (O(1) hash lookup), not `get_all_tags()` iteration
- [ ] Custom calculations are lightweight — heavy work belongs in the ability script, not the calc
- [ ] Large numbers of active effects on one entity are a design smell (>20 is suspicious)
- [ ] Tick intervals < 0.1s on many entities simultaneously may impact frame rate

**Why:** Per-frame effect application creates garbage. `_fast()` from GDScript bypasses safety checks and can corrupt state. Many fast-ticking effects consume significant frame budget.

---

## Quick Smoke Test

After implementation, mentally trace these scenarios:

1. **Happy path:** Entity activates ability → requirements met → tasks execute → effect applied → ability ends
2. **Blocked path:** Entity tries to activate → blocked_tag present → activation rejected → no side effects
3. **Cancel path:** Ability activating → entity gets stunned → ability cancelled → granted tags removed → cleanup complete
4. **Stack path:** Effect applied 3 times → stacks accumulate → duration refreshes (or not) → all stacks expire → tags removed
5. **Death path:** Entity health reaches 0 → all abilities cancelled → "State.Dead" tag added → no new abilities can activate
6. **Tick path:** Duration effect applied → ticks fire at interval → tick damage applies → effect expires → tag removed → no more ticks
7. **Source death path:** Buff source dies → `remove_all_effects_from_source()` called → buffs removed from all targets → tags cleaned up
8. **Preview path:** UI queries `preview_effect_on_attribute()` → returns value → no state mutated → actual application matches preview
