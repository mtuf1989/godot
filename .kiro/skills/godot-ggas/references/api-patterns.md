# GGAS API Patterns

Common implementation patterns for the GGAS plugin. Each section is self-contained — read only what the current task needs.

## Table of Contents

1. [ASC Setup](#asc-setup)
2. [Defining Abilities](#defining-abilities)
3. [Executing Abilities (Two-Tier)](#executing-abilities-two-tier)
4. [Defining Effects](#defining-effects)
5. [Periodic Tick Effects](#periodic-tick-effects)
6. [Attribute Sets](#attribute-sets)
7. [Tag Taxonomy Design](#tag-taxonomy-design)
8. [Custom Execution Calculations](#custom-execution-calculations)
9. [Async Ability Flows with Tasks](#async-ability-flows-with-tasks)
10. [Gameplay Cues](#gameplay-cues)
11. [Signal Wiring](#signal-wiring)
12. [Direct Effect Application](#direct-effect-application)
13. [Effect Query & Removal](#effect-query--removal)
14. [Modifier Preview](#modifier-preview)
15. [Known Gotchas](#known-gotchas)

---

## Property Name Quick Reference

Resource property names are verbose to avoid ambiguity in Godot's flat export namespace. This table maps what you'd guess to what it's actually called:

| You might type | Actual property | On which resource |
|---------------|----------------|-------------------|
| `duration` | `duration_magnitude` | `GameplayEffectResource` |
| `magnitude` / `value` | `magnitude_value` | `GameplayEffectModifier` |
| `attribute` | `attribute_name` | `GameplayEffectModifier` |
| `calc_type` | `magnitude_calculation` | `GameplayEffectModifier` |
| `caller_tag` | `magnitude_caller_tag` | `GameplayEffectModifier` |
| `source_attribute` | `magnitude_attribute` | `GameplayEffectModifier` |
| `channel` | `evaluation_channel` | `GameplayEffectModifier` |
| `id` / `name` | `ability_id` | `GameplayAbilityResource` |
| `script` | `ability_script_class` | `GameplayAbilityResource` |
| `exec_class` | `execution_calculation_class` | `GameplayEffectResource` |
| `tick_exec_class` | `tick_execution_calculation_class` | `GameplayEffectResource` |
| `tag` / `tag_name` | `tag_name` | `GameplayTagResource` |

---

## ASC Setup

Every entity that participates in the ability system needs an `AbilitySystemComponent` node.

```gdscript
@export var attribute_resource: AttributeSetResource

func _ready() -> void:
    var asc: AbilitySystemComponent = $AbilitySystemComponent
    
    # Owner = the logical owner (who gets credit for kills, etc.)
    asc.set_owner_actor(self)
    
    # Avatar = the physical body (same as owner for most characters)
    asc.set_avatar_actor(self)
    
    # Assign attribute set — accepts GDScript resource directly (auto-unwraps)
    asc.set_attribute_set_from_resource(attribute_resource)
    
    # Grant starting abilities
    asc.grant_ability("Ability.Attack.Basic", 1)
    asc.grant_ability("Ability.Dash", 1)
    
    # Configure ability costs and cooldowns
    asc.set_ability_costs(&"Ability.Dash", [
        {"attribute_name": "stamina", "cost_amount": 20.0}
    ])
    asc.set_ability_cooldown(&"Ability.Dash", 3.0)
    asc.set_ability_activation_tags(&"Ability.Dash", ["State.Dashing"])
    
    # Connect signals
    asc.attribute_changed.connect(_on_attribute_changed)
    asc.ability_activated.connect(_on_ability_activated)
    asc.tag_added.connect(_on_tag_added)
    asc.effect_ticked.connect(_on_effect_ticked)
```

**Owner vs Avatar distinction:** For player characters, both point to the same node. For summons/pets, the owner is the summoner (gets XP, credit) while the avatar is the pet (takes damage, plays animations).

**Source safety:** The ASC stores source references as `ObjectID` (not raw pointers). If the source node is freed, queries like `remove_all_effects_from_source()` safely handle the stale reference.

---

## Defining Abilities

Create a `GameplayAbilityResource` as a `.tres` file:

```gdscript
var ability := GameplayAbilityResource.new()
ability.ability_id = "Ability.Attack.Fireball"
ability.display_name = "Fireball"
ability.instancing_policy = 0  # Non_Instanced

# Activation requirements
ability.required_tags = [preload("res://data/tags/state_can_cast.tres")]
ability.blocked_tags = [preload("res://data/tags/status_stunned.tres")]

# Costs
ability.activation_costs = [
    {"attribute_name": "mana", "cost_amount": 25.0}
]

# Cooldown
ability.cooldown_duration = 2.0
ability.cooldown_tags = [preload("res://data/tags/cooldown_fireball.tres")]

# Tags granted while active
ability.granted_tags = [preload("res://data/tags/state_casting.tres")]
```

**Instancing policies:**
- `Non_Instanced` (0) — one shared instance, cheapest. Use for most abilities.
- `Instanced_Per_Actor` (1) — one instance per entity. Use when ability stores per-entity state.
- `Instanced_Per_Execution` (2) — new instance each activation. Use for overlapping abilities.

---

## Executing Abilities (Two-Tier)

### Tier 1 — Instant Abilities (`execute_ability`)

For the 80% case: validate, deduct costs, fire signal, done.

```gdscript
# Simple instant ability — no async, no tasks
var success = asc.execute_ability(&"Ability.Attack.Basic", {"target": target_node})

# The ASC automatically:
# 1. Validates activation (required/blocked tags, cooldown)
# 2. Checks and deducts configured costs
# 3. Starts cooldown timer
# 4. Grants activation_granted_tags (auto-removed immediately for instant)
# 5. Emits ability_activated signal with context
# 6. Emits ability_ended signal
```

Configure costs and cooldowns per-ability:

```gdscript
# During setup (e.g., in _ready or after granting)
asc.set_ability_costs(&"Ability.Attack.Basic", [
    {"attribute_name": "stamina", "cost_amount": 10.0}
])
asc.set_ability_cooldown(&"Ability.Attack.Basic", 1.5)
asc.set_ability_activation_tags(&"Ability.Attack.Basic", ["State.Attacking"])
```

### Tier 2 — Async Abilities (Full Flow)

For complex multi-step abilities with targeting, animations, and delayed effects:

```gdscript
# Full async flow with context
var context = ability_resource.create_activation_context(asc)
asc.try_activate_ability_with_context(&"Ability.Attack.Fireball", {
    "context": context
})
```

Choose Tier 1 when: instant damage, simple heals, toggles, AI-triggered attacks.
Choose Tier 2 when: player targeting, charge-up attacks, channeled spells, multi-step sequences.

---

## Defining Effects

### Instant Damage Effect

```gdscript
var effect := GameplayEffectResource.new()
effect.duration_policy = 0  # Instant

var mod := GameplayEffectModifier.new()
mod.attribute_name = "health"
mod.operation = 0  # Add (negative for damage)
mod.magnitude_calculation = 0  # Scalable_Float
mod.magnitude_value = -50.0
effect.modifiers = [mod]
```

### Duration Buff

```gdscript
var speed_buff := GameplayEffectResource.new()
speed_buff.duration_policy = 2  # Has_Duration
speed_buff.duration_magnitude = 10.0  # 10 seconds

var mod := GameplayEffectModifier.new()
mod.attribute_name = "movement_speed"
mod.operation = 1  # Multiply (bias-corrected: pass 1.3 for +30%)
mod.magnitude_value = 1.3  # +30% speed (system extracts 0.3 bonus)
speed_buff.modifiers = [mod]

# Grant a tag while active (auto-managed lifecycle with refcounting)
speed_buff.granted_tags = [preload("res://data/tags/status_hasted.tres")]

# Stacking
speed_buff.allow_stacking = true
speed_buff.max_stack_count = 3
speed_buff.refresh_duration_on_stack = true
```

### Infinite Passive (Equipment/Aura)

```gdscript
var armor_bonus := GameplayEffectResource.new()
armor_bonus.duration_policy = 1  # Infinite — stays until explicitly removed

var mod := GameplayEffectModifier.new()
mod.attribute_name = "defense"
mod.operation = 0  # Add
mod.magnitude_value = 15.0
armor_bonus.modifiers = [mod]
```

### Set_By_Caller Magnitude

When magnitude is determined at runtime (charge-up attacks, variable healing):

```gdscript
var mod := GameplayEffectModifier.new()
mod.attribute_name = "health"
mod.operation = 0  # Add
mod.magnitude_calculation = 2  # Set_By_Caller
mod.magnitude_caller_tag = "Data.Damage.Amount"

# At application time:
var context := {"Data.Damage.Amount": -charged_damage}
asc.try_activate_ability_with_context("Ability.Attack.Charged", context)
```

---

## Periodic Tick Effects

For DoT (damage over time) and HoT (healing over time), configure tick properties on the effect resource:

```gdscript
var poison_dot := GameplayEffectResource.new()
poison_dot.duration_policy = 2  # Has_Duration
poison_dot.duration_magnitude = 8.0  # 8 seconds total

# Tick configuration
poison_dot.tick_interval = 2.0        # Tick every 2 seconds (= 4 ticks total)
poison_dot.tick_on_application = false # Don't tick immediately on apply
poison_dot.tick_execution_calculation_class = "PoisonTickCalculation"

# Grant a tag while active (auto-managed)
poison_dot.granted_tags = [preload("res://data/tags/status_poisoned.tres")]
```

### Tick Execution Calculation

```gdscript
class_name PoisonTickCalculation
extends CustomExecutionCalculation

func execute_calculation(spec, target) -> void:
    var source_int = get_source_attribute_value(spec, "intelligence")
    var base_tick_damage = 5.0 * (1.0 + source_int * 0.02)
    
    # stack_count is available via spec — damage scales with stacks
    apply_modifier_to_target(target, "health", ModifierOperation.MODIFIER_SUBTRACT, base_tick_damage)
```

### Signal Handling for Ticks

```gdscript
asc.effect_ticked.connect(func(effect_id: StringName, stack_count: int, tick_number: int):
    print("Tick #%d of %s (stacks: %d)" % [tick_number, effect_id, stack_count])
)
```

### HoT (Heal Over Time) Example

```gdscript
var regen := GameplayEffectResource.new()
regen.duration_policy = 2
regen.duration_magnitude = 12.0
regen.tick_interval = 3.0          # Heal every 3s
regen.tick_on_application = true   # Immediate first heal

var mod := GameplayEffectModifier.new()
mod.attribute_name = "health"
mod.operation = 0  # Add
mod.magnitude_value = 15.0  # +15 HP per tick
regen.modifiers = [mod]
```

**Key behavior:**
- If `tick_execution_calculation_class` is empty, the effect's own modifiers are re-applied each tick
- If set, the calc class runs instead (more flexible, can read source stats)
- Tick damage/healing multiplies by `stack_count`
- The `effect_ticked` signal fires after each tick

### Testing Tick Effects with process_effects()

`process_effects(delta)` manually advances tick timers without waiting for real time. This is the primary tool for testing periodic effects:

```gdscript
# Apply a DoT with 2s tick interval
target_asc.apply_effect(poison_effect, 1, source_asc)

# Advance 2 seconds — first tick fires
target_asc.process_effects(2.0)
assert(health_decreased)

# Advance 2 more seconds — second tick fires
target_asc.process_effects(2.0)
```

Also useful for pause-aware systems where `_process()` is disabled and you drive ticks manually from a game clock.

---

## Attribute Sets

### From Code

```gdscript
var attrs := AttributeSetResource.new()
attrs.attributes = {
    "health": 100.0,
    "max_health": 100.0,
    "mana": 50.0,
    "max_mana": 50.0,
    "strength": 10.0,
    "defense": 5.0,
    "movement_speed": 1.0
}
```

### As .tres File

```ini
[gd_resource type="Resource" script_class="AttributeSetResource" load_steps=2 format=3]

[ext_resource type="Script" path="res://addons/ggas/resources/attribute_set_resource.gd" id="1"]

[resource]
script = ExtResource("1")
attributes = {
"health": 100.0,
"max_health": 100.0,
"mana": 50.0,
"max_mana": 50.0,
"strength": 10.0,
"defense": 5.0,
"movement_speed": 1.0
}
```

### Wiring to ASC

```gdscript
@export var attribute_resource: AttributeSetResource

func _ready() -> void:
    var asc: AbilitySystemComponent = $AbilitySystemComponent
    # Auto-unwraps — no need to call get_cpp_attribute_set() manually
    asc.set_attribute_set_from_resource(attribute_resource)
```

The ASC internally holds a `Ref<Resource>` to prevent the GDScript wrapper from being garbage collected.

### Runtime Access

```gdscript
# Read current value (reflects all active modifiers)
var current_health = asc.get_attribute_value(&"health")

# Read base value (no modifiers)
var base_health = attribute_resource.get_attribute(&"health")

# Write base value
attribute_resource.set_attribute(&"health", 80.0)
```

**Clamping:** Use `clamp_rules` on `AttributeSetResource` or call `set_clamp_rule()` / `set_clamp_rule_dynamic()` on the C++ `AttributeSet`:

```gdscript
# Via resource (preferred — declarative)
var attrs := AttributeSetResource.new()
attrs.attributes = {"health": 100.0, "max_health": 100.0, "mana": 50.0, "max_mana": 50.0}
attrs.clamp_rules = {
    "health": {"min_value": 0.0, "max_attribute": "max_health"},
    "mana": {"min_value": 0.0, "max_attribute": "max_mana"}
}

# Via C++ API (imperative — for dynamic setup)
attr_set.set_clamp_rule(&"health", 0.0, 999.0)
attr_set.set_clamp_rule_dynamic(&"health", &"", &"max_health")
```

Clamping is applied after modifier aggregation, before `attribute_changed` fires. Reducing `max_health` automatically re-clamps `health` on next read.

---

## Tag Taxonomy Design

Tags use dot-separated hierarchies. The hierarchy enables broad matching:

```
# has_tag("Status.Debuff") matches ALL of these:
Status.Debuff.Burning
Status.Debuff.Frozen
Status.Debuff.Poisoned

# has_tag_exact("Status.Debuff.Burning") matches ONLY Burning
```

**Recommended taxonomy:**

```
Ability.Attack.Basic
Ability.Attack.Fireball
Ability.Movement.Dash
Ability.Defense.Block

Status.Buff.Haste
Status.Buff.Shield
Status.Debuff.Burning
Status.Debuff.Stunned
Status.Debuff.Poisoned

State.CanCast
State.CanMove
State.Casting
State.Dashing
State.Dead

Cooldown.Ability.Fireball
Cooldown.Ability.Dash

Immunity.Status.Stun
Immunity.Status.Fire

Data.Damage.Amount
Data.Heal.Amount
```

**Tag lifecycle for effects:**
- Tags in `granted_tags` are automatically added on effect application and removed on expiry/removal
- Ref-counted: two effects granting `Status.Debuff.Burning` → tag persists until both expire
- No manual `add_tag()`/`remove_tag()` needed for effect-managed tags

---

## Custom Execution Calculations

For damage formulas, resistance calculations, or any logic beyond simple modifiers:

```gdscript
class_name PoisonDamageCalculation
extends CustomExecutionCalculation

@export var base_damage_per_tick: float = 5.0
@export var max_resistance_percent: float = 75.0

func execute_calculation(spec, target) -> void:
    var source_int := get_source_attribute_value(spec, "intelligence")
    var scaling := 1.0 + (source_int * 0.02)
    var raw_damage := base_damage_per_tick * scaling
    
    var resistance := get_target_attribute_value(target, "poison_resistance")
    resistance = clampf(resistance, 0.0, max_resistance_percent)
    var final_damage := raw_damage * (1.0 - resistance / 100.0)
    
    apply_modifier_to_target(target, "health", ModifierOperation.MODIFIER_SUBTRACT, final_damage)

func can_execute_calculation(spec, target) -> bool:
    return not target_has_tag(target, "State.Dead")
```

**When to use:**
- Simple flat/percentage changes → `GameplayEffectModifier`
- Multi-attribute formulas, conditionals, source stats → `CustomExecutionCalculation`
- Keep game logic (spawning, signals, events) OUT of calculations — that belongs in the ability script

---

## Async Ability Flows with Tasks

Tasks enable multi-step ability execution with `await`:

```gdscript
func activate_ability(context: AbilityActivationContext) -> void:
    var asc = context.source_asc
    
    # Step 1: Wait for targeting
    var targeting := WaitForTargetData.new()
    targeting.targeting_mode = WaitForTargetData.TargetingMode.SINGLE_TARGET
    targeting.max_range = 20.0
    targeting.activation_context = context
    targeting.source_asc = asc
    
    await targeting.execute()
    if targeting.is_cancelled:
        context.cancel("No target selected")
        return
    
    # Step 2: Play cast animation
    var montage := PlayMontageAndWait.new()
    montage.animation_player = asc.get_avatar_actor().get_node("AnimationPlayer")
    montage.animation_name = "cast_fireball"
    montage.activation_context = context
    montage.source_asc = asc
    
    await montage.execute()
    if montage.is_cancelled:
        return
    
    # Step 3: Apply effect (type-safe)
    var target_asc = targeting.target_data["target_asc"]
    var effect_res = preload("res://data/effects/fire_damage.tres")
    target_asc.apply_effect(effect_res, 1, asc)
    
    # Step 4: Trigger cue
    asc.trigger_gameplay_cue("Cue.Ability.Fireball.Impact", {
        "location": targeting.target_data["hit_position"]
    })
    
    context.end_ability()
```

**Built-in tasks:** `WaitDelay`, `WaitForTargetData`, `PlayMontageAndWait`, `WaitForEvent`

### Creating Custom Tasks

Override `_start_task_implementation()` — NOT `execute()`. Resolve via `complete_task()`, `cancel_task()`, or `fail_task()`:

```gdscript
class_name WaitForHealthThreshold
extends AbilityTask

var target_asc: AbilitySystemComponent
var threshold: float = 0.5  # 50% health

func _start_task_implementation() -> void:
    # Connect to signals, start polling, etc.
    target_asc.attribute_changed.connect(_on_attribute_changed)
    register_cleanup_function(_disconnect_signals)

func _on_attribute_changed(attr: StringName, old: float, new_val: float) -> void:
    if attr == &"health":
        var max_hp = target_asc.get_attribute_value(&"max_health")
        if max_hp > 0 and new_val / max_hp <= threshold:
            complete_task({"health_percent": new_val / max_hp})

func _disconnect_signals() -> void:
    if target_asc and target_asc.attribute_changed.is_connected(_on_attribute_changed):
        target_asc.attribute_changed.disconnect(_on_attribute_changed)
```

---

## Gameplay Cues

Cues bridge GGAS events to visual/audio feedback:

```gdscript
func _ready() -> void:
    var cue_manager: GameplayCueManager = $GameplayCueManager
    asc.set_cue_manager(cue_manager)
    cue_manager.register_cue(preload("res://data/cues/fireball_impact.tres"))

# Trigger from ability or effect
asc.trigger_gameplay_cue("Cue.Ability.Fireball.Impact", {
    "location": hit_pos,
    "intensity": damage_dealt / max_damage
})

# Stop looping cue
asc.stop_gameplay_cue("Cue.Status.Burning")
```

---

## Signal Wiring

```gdscript
# UI: Health bar
asc.attribute_changed.connect(func(attr: StringName, old: float, new_val: float):
    if attr == &"health":
        health_bar.value = new_val / asc.get_attribute_value("max_health")
)

# AI: React to debuffs
asc.tag_added.connect(func(tag: StringName):
    if tag == &"Status.Debuff.Stunned":
        state_machine.transition_to("stunned")
)

# Feedback: Tick damage numbers
asc.effect_ticked.connect(func(effect_id: StringName, stack_count: int, tick_number: int):
    # Show floating damage number on each tick
    _show_tick_damage(effect_id, stack_count)
)

# Cleanup: Source entity died — remove all its effects from us
func _on_source_died(source_entity: Node) -> void:
    var source_asc = source_entity.get_node("AbilitySystemComponent")
    asc.remove_all_effects_from_source(source_asc)
```

---

## Direct Effect Application

### Type-Safe Application (Preferred)

```gdscript
# Preloaded resource — rename-safe, type-safe
var effect = preload("res://data/effects/fire_damage.tres")
target_asc.apply_effect(effect, 1, source_asc)
```

`apply_effect()` accepts either:
- A C++ `GameplayEffect` directly
- A GDScript `GameplayEffectResource` (auto-calls `get_cpp_effect()` internally)

### Inline Effect (Ad-hoc)

```gdscript
# Quick inline damage — no resource file needed
target_asc.apply_gameplay_effect(&"Effect.Damage.Physical", 0.0, 1, [
    {"attribute": &"health", "operation": 0, "magnitude": -50.0}
], attacker_asc)
```

### With CustomExecutionCalculation

```gdscript
# Source must be passed for get_source_attribute_value() to work
var cpp_effect = effect_res.get_cpp_effect()
target_asc.apply_gameplay_effect_resource(cpp_effect, level, attacker_asc)
```

### When to Use Each

| Scenario | Method |
|----------|--------|
| Standard effect application | `apply_effect(preloaded_resource, level, source)` |
| Quick inline damage/heal | `apply_gameplay_effect(id, duration, stacks, mods, source)` |
| Legacy/advanced usage | `apply_gameplay_effect_resource(cpp_effect, level, source)` |

---

## Effect Query & Removal

### Query Active Effects

```gdscript
# How many burn stacks?
var burn_stacks = asc.get_effect_stack_count_by_tag(&"Status.Debuff.Burning")

# How long until poison expires?
var remaining = asc.get_effect_remaining_duration_by_tag(&"Status.Debuff.Poisoned")

# Get full details of all active effects
var all_effects: Array = asc.get_active_effects_detailed()
# Returns Array of Dictionary:
# { "effect_id", "stack_count", "remaining_duration", "elapsed_time", "source" }

# Get effects matching a specific tag
var burns: Array = asc.get_active_effects_with_tag(&"Status.Debuff.Burning")
```

### Remove Effects

```gdscript
# Remove all effects granting a specific tag
var removed_count = asc.remove_effects_with_tag(&"Status.Debuff.Burning")

# Remove a specific effect by ID
asc.remove_effect_by_id(&"Effect.Buff.Haste")

# Remove all effects from a source (e.g., source died)
var removed = asc.remove_all_effects_from_source(source_asc)
```

**Important:** `remove_effects_with_tag()` matches on `granted_tags` — it removes effects that *grant* the specified tag, not effects that merely *require* it.

---

## Modifier Preview

For UI tooltips, equipment comparison, or "what-if" calculations:

```gdscript
# Preview: "What would health be with these hypothetical modifiers?"
var preview_health = asc.preview_attribute_value(&"health", [
    {"operation": 0, "magnitude": -25.0},  # -25 damage
])

# Preview: "What would this effect do to defense?"
var effect = preload("res://data/effects/armor_buff.tres")
var preview_defense = asc.preview_effect_on_attribute(
    effect.get_cpp_effect(), &"defense", 1
)

# Use for tooltip: "Equipping this would change defense from X to Y"
var current = asc.get_attribute_value(&"defense")
var tooltip = "Defense: %d → %d" % [current, preview_defense]
```

**Limitations:**
- Set-by-caller magnitudes use 0 unless passed in hypothetical modifiers
- Previews are point-in-time — attribute-based magnitudes use current values

### Previewing Custom Execution Calculations

By default, effects with `execution_calculation_class` return the current value (no change shown). To enable preview, override `_preview_calculation` in your calc class:

```gdscript
class_name PhysicalDamageCalculation
extends CustomExecutionCalculation

func execute_calculation(spec, target) -> void:
    var atk = get_source_attribute_value(spec, "attack_power")
    var def = get_target_attribute_value(target, "defense")
    var damage = maxf(atk - def, 1.0)
    apply_modifier_to_target(target, "health", ModifierOperation.MODIFIER_ADD, -damage)

## Override for UI tooltip previews (read-only, no state mutation)
func _preview_calculation(spec, target, level: int) -> Dictionary:
    var atk = get_source_attribute_value(spec, "attack_power")
    var def = get_target_attribute_value(target, "defense")
    var damage = maxf(atk - def, 1.0)
    return {"health": -damage}  # Negative = damage
```

Return a Dictionary of `{attribute_name: delta_value}`. The system adds the delta to the current value.

---

## Known Gotchas

### Source Attribution in CustomExecutionCalculation

Always pass the source ASC when applying effects that use custom calculations:

```gdscript
# CORRECT — source passed, get_source_attribute_value() works
target_asc.apply_effect(effect_res, 1, attacker_asc)

# WRONG — no source, get_source_attribute_value() returns 0
target_asc.apply_effect(effect_res)
```

### Effect Stacking with Same ID

Calling `apply_gameplay_effect()` with the same `effect_id` increments stacks. For separate instances (multiple DoTs from different sources), use unique IDs:

```gdscript
var unique_id = &"Effect.DoT.Poison_%d" % Time.get_ticks_msec()
target_asc.apply_gameplay_effect(unique_id, 5.0, 1, [...])
```

### Granted Tags Are Ref-Counted

Two effects granting `Status.Debuff.Burning`:
- After first effect applied: tag present (refcount = 1)
- After second effect applied: tag still present (refcount = 2)
- After first effect expires: tag still present (refcount = 1)
- After second effect expires: tag removed (refcount = 0)

Do NOT manually `remove_tag()` for tags managed by effects — it would desync the refcount.

### Tick Effects Don't Apply Persistent Modifiers

Ticking effects fire their modifiers/calc on each tick as a one-shot base value change. They do NOT add persistent modifiers that stack over time. Each tick is an independent application.

### process_effects() for Testing

See [Testing Tick Effects](#testing-tick-effects-with-process_effects) in the Periodic Tick section.

### set_attribute_set_from_resource vs set_attribute_set

- `set_attribute_set_from_resource(resource)` — accepts GDScript `AttributeSetResource` directly, keeps reference alive
- `set_attribute_set(cpp_attr_set)` — legacy, requires manual `resource.get_cpp_attribute_set()` call

Prefer `set_attribute_set_from_resource()` for all new code.
