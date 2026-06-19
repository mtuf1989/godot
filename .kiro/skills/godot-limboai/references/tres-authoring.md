# BehaviorTree .tres Text Authoring

Generate valid LimboAI BehaviorTree `.tres` files from text without the editor.

## File Grammar

A `.tres` BehaviorTree file has four ordered sections:

```
[gd_resource type="BehaviorTree" format=3]        ← 1. Header (required)

[ext_resource type="Script" path="..." id="..."]   ← 2. External resources (optional)

[sub_resource type="..." id="..."]                 ← 3. Sub-resources (the tree nodes)
property = value

[resource]                                         ← 4. Root resource (required)
blackboard_plan = SubResource("...")
root_task = SubResource("...")
```

### Rules

- Every `sub_resource` needs a unique `id` within the file. Use `Type_xxx` format (e.g., `BTSequence_001`).
- The `id` is an arbitrary string — use counters, random suffixes, or descriptive names.
- `uid="uid://..."` in the header is optional. Godot generates one on first load if missing.
- Properties only need to be listed when they differ from the default value.
- Order of `sub_resource` blocks doesn't matter for correctness, but define dependencies before dependents for readability.

---

## Value Syntax

| GDScript Type | .tres Syntax | Example |
|---|---|---|
| int | bare number | `42` |
| float | number with decimal | `1.5` |
| bool | `true` / `false` | `true` |
| String | `"quoted"` | `"hello"` |
| StringName | `&"quoted"` | `&"speed"` |
| NodePath | `NodePath("path")` | `NodePath("AnimationPlayer")` |
| Vector2 | `Vector2(x, y)` | `Vector2(100, 200)` |
| Vector3 | `Vector3(x, y, z)` | `Vector3(0, 1, 0)` |
| Color | `Color(r, g, b, a)` | `Color(1, 0, 0, 1)` |
| Array (typed) | `Array[Type]([...])` | `Array[BBVariant]([SubResource("v1")])` |
| SubResource ref | `SubResource("id")` | `SubResource("BTWait_001")` |
| ExtResource ref | `ExtResource("id")` | `ExtResource("1_action")` |
| null | `null` | `null` |
| PackedStringArray | `PackedStringArray("a", "b")` | `PackedStringArray("x", "delta")` |

### Variant Type Integers (for BlackboardPlan and BBVariant)

| ID | Type | Example value |
|----|------|---------------|
| 0 | Nil | `null` |
| 1 | bool | `true` |
| 2 | int | `42` |
| 3 | float | `3.14` |
| 4 | String | `"text"` |
| 5 | Vector2 | `Vector2(0, 0)` |
| 6 | Vector3 | `Vector3(0, 0, 0)` |
| 7 | Rect2 | `Rect2(0, 0, 0, 0)` |
| 20 | NodePath | `NodePath("")` |
| 21 | StringName | `&""` |
| 24 | Object | `null` |
| 28 | Array | `[]` |

### Property Hint Integers (for BlackboardPlan variables)

| ID | Hint | hint_string example |
|----|------|---------------------|
| 0 | NONE | `""` |
| 1 | RANGE | `"10,1000,10"` (min,max,step) |
| 2 | ENUM | `"Option1,Option2"` |

---

## Section 1: Header

```ini
[gd_resource type="BehaviorTree" format=3]
```

Optionally include a UID (Godot auto-generates if missing):
```ini
[gd_resource type="BehaviorTree" format=3 uid="uid://abc123"]
```

---

## Section 2: External Resources

Reference custom task scripts. Each needs a unique `id`.

```ini
[ext_resource type="Script" path="res://ai/tasks/actions/patrol.gd" id="1_patrol"]
[ext_resource type="Script" path="res://ai/tasks/conditions/is_player_near.gd" id="2_near"]
```

For BTSubtree referencing another .tres:
```ini
[ext_resource type="BehaviorTree" path="res://ai/trees/combat_subtree.tres" id="3_subtree"]
```

**ID format**: Any unique string. Convention: `<counter>_<shortname>`.

**UID in ext_resource**: Optional. If you know it, include `uid="uid://..."` before `path=`. If omitted, Godot resolves by path.

---

## Section 3: Sub-resources

### BlackboardPlan

Defines variables available to the tree. Each variable needs 5 properties:

```ini
[sub_resource type="BlackboardPlan" id="BlackboardPlan_001"]
var/speed/name = &"speed"
var/speed/type = 3
var/speed/value = 400.0
var/speed/hint = 1
var/speed/hint_string = "10,1000,10"
var/target/name = &"target"
var/target/type = 24
var/target/value = null
var/target/hint = 0
var/target/hint_string = ""
var/is_alerted/name = &"is_alerted"
var/is_alerted/type = 1
var/is_alerted/value = false
var/is_alerted/hint = 0
var/is_alerted/hint_string = ""
```

Pattern: `var/<key>/property` where `<key>` is the variable's internal key (usually same as name).

### BBNode (node reference parameter)

Used by tasks that need a node reference (e.g., AnimationPlayer):

```ini
[sub_resource type="BBNode" id="BBNode_anim"]
resource_name = "AnimationPlayer"
saved_value = NodePath("AnimationPlayer")
```

To reference the agent itself (the node owning the BTPlayer):
```ini
[sub_resource type="BBNode" id="BBNode_self"]
resource_name = "."
saved_value = NodePath(".")
```

To reference from a blackboard variable instead of a saved path:
```ini
[sub_resource type="BBNode" id="BBNode_dynamic"]
value_source = 1
variable = &"target_node"
```

### BBVariant (typed value parameter)

Used by BTCheckVar, BTSetVar, BTCallMethod args, etc.:

```ini
# Literal integer value
[sub_resource type="BBVariant" id="BBVariant_six"]
resource_name = "6"
saved_value = 6
type = 2

# Literal bool
[sub_resource type="BBVariant" id="BBVariant_false"]
resource_name = "false"
saved_value = false
type = 1

# Literal float
[sub_resource type="BBVariant" id="BBVariant_speed"]
saved_value = 200.0
type = 3

# Reference a blackboard variable instead of a literal
[sub_resource type="BBVariant" id="BBVariant_from_bb"]
resource_name = "$target_pos"
value_source = 1
variable = &"target_pos"
type = 5
```

Key: `value_source = 1` means "read from blackboard variable" instead of using `saved_value`.

---

## Section 4: Task Nodes

### Composites (have `children` array)

```ini
[sub_resource type="BTSequence" id="BTSequence_main"]
custom_name = "Main Logic"
children = [SubResource("BTAction_001"), SubResource("BTWait_001")]

[sub_resource type="BTSelector" id="BTSelector_root"]
children = [SubResource("BTSequence_combat"), SubResource("BTSequence_idle")]

[sub_resource type="BTParallel" id="BTParallel_001"]
num_successes_required = 2
num_failures_required = 1
repeat = true
children = [SubResource("BTAction_move"), SubResource("BTCondition_check")]
```

Available composites: `BTSequence`, `BTSelector`, `BTParallel`, `BTDynamicSequence`, `BTDynamicSelector`, `BTRandomSequence`, `BTRandomSelector`, `BTProbabilitySelector`.

#### BTProbabilitySelector weights

Child weights are stored as metadata on each child task:
```ini
[sub_resource type="BTSequence" id="BTSequence_attack"]
custom_name = "Attack"
children = [...]
metadata/_weight_ = 3.0

[sub_resource type="BTSequence" id="BTSequence_flee"]
custom_name = "Flee"
children = [...]
metadata/_weight_ = 1.0

[sub_resource type="BTProbabilitySelector" id="BTProbSelector_001"]
children = [SubResource("BTSequence_attack"), SubResource("BTSequence_flee")]
```

### Decorators (have `children` array with single child)

```ini
[sub_resource type="BTRepeat" id="BTRepeat_001"]
children = [SubResource("BTAction_patrol")]
times = 3
abort_on_failure = true

[sub_resource type="BTCooldown" id="BTCooldown_001"]
children = [SubResource("BTSequence_attack")]
duration = 5.0
cooldown_state_var = &"attack_cooldown"

[sub_resource type="BTTimeLimit" id="BTTimeLimit_001"]
children = [SubResource("BTAction_pursue")]
time_limit = 3.0

[sub_resource type="BTProbability" id="BTProbability_001"]
children = [SubResource("BTSequence_taunt")]
run_chance = 0.3

[sub_resource type="BTRunLimit" id="BTRunLimit_001"]
children = [SubResource("BTSequence_intro")]
run_limit = 1
count_policy = 2

[sub_resource type="BTForEach" id="BTForEach_001"]
children = [SubResource("BTAction_process")]
array_var = &"enemies"
save_var = &"current_enemy"

[sub_resource type="BTInvert" id="BTInvert_001"]
children = [SubResource("BTCondition_alive")]

[sub_resource type="BTNewScope" id="BTNewScope_001"]
children = [SubResource("BTSequence_isolated")]
blackboard_plan = SubResource("BlackboardPlan_scope")
```

#### BTSubtree (references external .tres)

```ini
[sub_resource type="BTSubtree" id="BTSubtree_001"]
subtree = ExtResource("3_subtree")
```

### Actions (leaf nodes, no children)

```ini
# Built-in wait
[sub_resource type="BTWait" id="BTWait_001"]
duration = 1.5

# Built-in random wait
[sub_resource type="BTRandomWait" id="BTRandomWait_001"]
min_duration = 0.5
max_duration = 2.0

# Play animation
[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_001"]
animation_player = SubResource("BBNode_anim")
animation_name = &"run"
await_completion = 2.0
blend = 0.1

# Call a method on a node
[sub_resource type="BTCallMethod" id="BTCallMethod_001"]
node = SubResource("BBNode_self")
method = &"fire_projectile"
args = Array[BBVariant]([SubResource("BBVariant_from_bb")])

# Call method and store result
[sub_resource type="BTCallMethod" id="BTCallMethod_002"]
node = SubResource("BBNode_self")
method = &"get_nearest_cover"
result_var = &"cover_position"

# Set agent property
[sub_resource type="BTSetAgentProperty" id="BTSetAgentProp_001"]
property = &"velocity"
value = SubResource("BBVariant_speed")

# Set blackboard variable
[sub_resource type="BTSetVar" id="BTSetVar_001"]
variable = &"health"
value = SubResource("BBVariant_six")
operation = 2

# Custom action (script-based)
[sub_resource type="BTAction" id="BTAction_patrol"]
script = ExtResource("1_patrol")
range_min = 200.0
patrol_speed = 150.0

# Always fail (utility)
[sub_resource type="BTFail" id="BTFail_001"]
```

### Conditions (leaf nodes, no children)

```ini
# Check blackboard variable
[sub_resource type="BTCheckVar" id="BTCheckVar_001"]
variable = &"health"
check_type = 1
value = SubResource("BBVariant_threshold")

# Check trigger (reads bool, resets to false)
[sub_resource type="BTCheckTrigger" id="BTCheckTrigger_001"]
variable = &"damage_received"

# Check agent property
[sub_resource type="BTCheckAgentProperty" id="BTCheckAgentProp_001"]
property = &"is_grounded"
check_type = 0
value = SubResource("BBVariant_true")

# Custom condition (script-based)
[sub_resource type="BTCondition" id="BTCondition_near"]
script = ExtResource("2_near")
detection_range = 300.0
```

### Comments (ignored at runtime)

```ini
[sub_resource type="BTComment" id="BTComment_001"]
custom_name = "Phase 1: Patrol until player detected"
```

Comments can be children of composites — they're skipped during execution.

---

## Section 5: Root Resource

```ini
[resource]
blackboard_plan = SubResource("BlackboardPlan_001")
root_task = SubResource("BTSelector_root")
```

Optional `description` field (BBCode supported):
```ini
[resource]
description = "Enemy patrol and combat behavior."
blackboard_plan = SubResource("BlackboardPlan_001")
root_task = SubResource("BTSelector_root")
```

---

## Enum Quick Reference

### check_type (LimboUtility.CheckType)
| Value | Meaning |
|-------|---------|
| 0 | EQUAL (==) |
| 1 | LESS_THAN (<) |
| 2 | LESS_THAN_OR_EQUAL (<=) |
| 3 | GREATER_THAN (>) |
| 4 | GREATER_THAN_OR_EQUAL (>=) |
| 5 | NOT_EQUAL (!=) |

### operation (LimboUtility.Operation)
| Value | Meaning |
|-------|---------|
| 0 | NONE (direct assign) |
| 1 | ADDITION (+) |
| 2 | SUBTRACTION (-) |
| 3 | MULTIPLICATION (*) |
| 4 | DIVISION (/) |
| 5 | MODULO (%) |
| 6 | POWER (**) |

### count_policy (BTRunLimit.CountPolicy)
| Value | Meaning |
|-------|---------|
| 0 | COUNT_SUCCESSFUL |
| 1 | COUNT_FAILED |
| 2 | COUNT_ALL |

---

## Complete Example: Patrol-Chase-Attack Enemy

This demonstrates a realistic game AI tree with blackboard, conditions, decorators, custom scripts, and built-in tasks working together.

```ini
[gd_resource type="BehaviorTree" format=3]

[ext_resource type="Script" path="res://ai/tasks/actions/select_patrol_point.gd" id="1_patrol"]
[ext_resource type="Script" path="res://ai/tasks/actions/move_to_position.gd" id="2_move"]
[ext_resource type="Script" path="res://ai/tasks/conditions/is_player_visible.gd" id="3_visible"]
[ext_resource type="Script" path="res://ai/tasks/actions/pursue_target.gd" id="4_pursue"]

; === BLACKBOARD ===

[sub_resource type="BlackboardPlan" id="BlackboardPlan_001"]
var/speed/name = &"speed"
var/speed/type = 3
var/speed/value = 200.0
var/speed/hint = 1
var/speed/hint_string = "10,800,10"
var/run_speed/name = &"run_speed"
var/run_speed/type = 3
var/run_speed/value = 450.0
var/run_speed/hint = 1
var/run_speed/hint_string = "10,800,10"
var/target/name = &"target"
var/target/type = 24
var/target/value = null
var/target/hint = 0
var/target/hint_string = ""
var/patrol_pos/name = &"patrol_pos"
var/patrol_pos/type = 5
var/patrol_pos/value = Vector2(0, 0)
var/patrol_pos/hint = 0
var/patrol_pos/hint_string = ""
var/attack_cooldown/name = &"attack_cooldown"
var/attack_cooldown/type = 1
var/attack_cooldown/value = false
var/attack_cooldown/hint = 0
var/attack_cooldown/hint_string = ""

; === SHARED NODE REFERENCES ===

[sub_resource type="BBNode" id="BBNode_anim"]
resource_name = "AnimationPlayer"
saved_value = NodePath("AnimationPlayer")

[sub_resource type="BBNode" id="BBNode_self"]
resource_name = "."
saved_value = NodePath(".")

; === PATROL BRANCH ===

[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_walk"]
animation_player = SubResource("BBNode_anim")
animation_name = &"walk"
blend = 0.1

[sub_resource type="BTAction" id="BTAction_select_patrol"]
script = ExtResource("1_patrol")

[sub_resource type="BTAction" id="BTAction_move_patrol"]
script = ExtResource("2_move")
target_var = &"patrol_pos"
speed_var = &"speed"

[sub_resource type="BTTimeLimit" id="BTTimeLimit_patrol"]
children = [SubResource("BTAction_move_patrol")]
time_limit = 8.0

[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_idle"]
animation_player = SubResource("BBNode_anim")
animation_name = &"idle"
blend = 0.1

[sub_resource type="BTRandomWait" id="BTRandomWait_pause"]
min_duration = 1.0
max_duration = 3.0

[sub_resource type="BTSequence" id="BTSequence_patrol"]
custom_name = "Patrol"
children = [SubResource("BTPlayAnimation_walk"), SubResource("BTAction_select_patrol"), SubResource("BTTimeLimit_patrol"), SubResource("BTPlayAnimation_idle"), SubResource("BTRandomWait_pause")]

; === CHASE BRANCH ===

[sub_resource type="BTCondition" id="BTCondition_visible"]
script = ExtResource("3_visible")
detection_range = 500.0
fov_degrees = 120.0

[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_run"]
animation_player = SubResource("BBNode_anim")
animation_name = &"run"
blend = 0.1

[sub_resource type="BTAction" id="BTAction_pursue"]
script = ExtResource("4_pursue")
speed_var = &"run_speed"

[sub_resource type="BTTimeLimit" id="BTTimeLimit_chase"]
children = [SubResource("BTAction_pursue")]
time_limit = 5.0

[sub_resource type="BTSequence" id="BTSequence_chase"]
custom_name = "Chase Player"
children = [SubResource("BTCondition_visible"), SubResource("BTPlayAnimation_run"), SubResource("BTTimeLimit_chase")]

; === ATTACK BRANCH ===

[sub_resource type="BBVariant" id="BBVariant_attack_range"]
saved_value = 80.0
type = 3

[sub_resource type="BTCheckAgentProperty" id="BTCheckAgentProp_inrange"]
property = &"distance_to_target"
check_type = 2
value = SubResource("BBVariant_attack_range")

[sub_resource type="BTCallMethod" id="BTCallMethod_face"]
node = SubResource("BBNode_self")
method = &"face_target"

[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_attack"]
await_completion = 1.5
animation_player = SubResource("BBNode_anim")
animation_name = &"attack"
blend = 0.05

[sub_resource type="BTCallMethod" id="BTCallMethod_damage"]
node = SubResource("BBNode_self")
method = &"deal_damage"

[sub_resource type="BTSequence" id="BTSequence_strike"]
custom_name = "Strike"
children = [SubResource("BTCallMethod_face"), SubResource("BTPlayAnimation_attack"), SubResource("BTCallMethod_damage")]

[sub_resource type="BTCooldown" id="BTCooldown_attack"]
children = [SubResource("BTSequence_strike")]
duration = 2.0
cooldown_state_var = &"attack_cooldown"

[sub_resource type="BTSequence" id="BTSequence_attack"]
custom_name = "Attack"
children = [SubResource("BTCheckAgentProp_inrange"), SubResource("BTCooldown_attack")]

; === ROOT ===

[sub_resource type="BTSelector" id="BTSelector_root"]
children = [SubResource("BTSequence_attack"), SubResource("BTSequence_chase"), SubResource("BTSequence_patrol")]

[resource]
blackboard_plan = SubResource("BlackboardPlan_001")
root_task = SubResource("BTSelector_root")
```

### Tree structure (visual):

```
Selector (root)
├── Sequence "Attack"
│   ├── CheckAgentProperty (distance_to_target <= 80)
│   └── Cooldown (2s)
│       └── Sequence "Strike"
│           ├── CallMethod (face_target)
│           ├── PlayAnimation (attack, await 1.5s)
│           └── CallMethod (deal_damage)
├── Sequence "Chase Player"
│   ├── Condition [is_player_visible] (range=500, fov=120)
│   ├── PlayAnimation (run)
│   └── TimeLimit (5s)
│       └── Action [pursue_target]
└── Sequence "Patrol"
    ├── PlayAnimation (walk)
    ├── Action [select_patrol_point]
    ├── TimeLimit (8s)
    │   └── Action [move_to_position]
    ├── PlayAnimation (idle)
    └── RandomWait (1-3s)
```

---

## Composition Principles

When generating a tree, think in these layers:

1. **Blackboard first** — define all variables the tree needs (speeds, targets, flags, positions).
2. **Shared BBNode refs** — create BBNode sub-resources for nodes referenced multiple times (AnimationPlayer, self).
3. **Leaf tasks bottom-up** — create the atomic actions and conditions.
4. **Compose into sequences** — group related leaves into named sequences.
5. **Add decorators** — wrap sequences with Cooldown, TimeLimit, Probability, Repeat as needed.
6. **Wire into root composite** — arrange branches in priority order (Selector) or sequential order (Sequence).

### Custom script properties

When a task uses `script = ExtResource(...)`, any `@export` variables from that script become properties on the sub_resource:

```gdscript
# res://ai/tasks/actions/move_to_position.gd
@export var target_var: StringName = &"target_pos"
@export var speed_var: StringName = &"speed"
@export var arrival_distance: float = 10.0
```

In .tres:
```ini
[sub_resource type="BTAction" id="BTAction_move"]
script = ExtResource("2_move")
target_var = &"target_pos"
speed_var = &"speed"
arrival_distance = 10.0
```

The property names and types match the `@export` declarations exactly.

### Reusing BBNode references

If multiple tasks reference the same node (e.g., AnimationPlayer), define one BBNode and reference it from all:

```ini
[sub_resource type="BBNode" id="BBNode_anim"]
resource_name = "AnimationPlayer"
saved_value = NodePath("AnimationPlayer")

[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_001"]
animation_player = SubResource("BBNode_anim")
...

[sub_resource type="BTPlayAnimation" id="BTPlayAnimation_002"]
animation_player = SubResource("BBNode_anim")
...
```

This is valid — multiple tasks can share the same BBNode sub_resource.

---

## Checklist Before Saving

1. Every `SubResource("X")` reference points to an existing `[sub_resource ... id="X"]`
2. Every `ExtResource("X")` reference points to an existing `[ext_resource ... id="X"]`
3. The `[resource]` section has both `blackboard_plan` and `root_task`
4. Blackboard variables referenced by tasks (via `variable = &"name"` or `speed_var = &"name"`) are declared in the BlackboardPlan
5. BBVariant `type` matches the actual `saved_value` type
6. Decorator `children` arrays have exactly one element
7. No circular SubResource references
