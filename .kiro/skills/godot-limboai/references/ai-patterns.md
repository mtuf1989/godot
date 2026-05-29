# Common Game AI Patterns with LimboAI

## Pattern 1: Patrol & Wander

A simple enemy that moves between waypoints or wanders randomly.

### BT Structure
```
Selector
├── Sequence [React to player]
│   ├── InRange (condition: player within detection range)
│   └── ... (chase/attack subtree)
└── Sequence [Patrol]
    ├── SelectRandomNearbyPos → stores to "patrol_pos"
    ├── ArriveTo "patrol_pos"
    └── BTWait (pause at waypoint)
```

### Custom Tasks Needed

**SelectRandomNearbyPos** (BTAction):
```gdscript
@tool
extends BTAction

@export var range_min: float = 200.0
@export var range_max: float = 400.0
@export var position_var: StringName = &"patrol_pos"

func _generate_name() -> String:
    return "SelectRandomNearbyPos [%s, %s] ➜%s" % [
        range_min, range_max, LimboUtility.decorate_var(position_var)]

func _tick(_delta: float) -> Status:
    var angle := randf() * TAU
    var dist := randf_range(range_min, range_max)
    var pos: Vector2 = agent.global_position + Vector2.from_angle(angle) * dist
    blackboard.set_var(position_var, pos)
    return SUCCESS
```

**MoveToPosition** (BTAction):
```gdscript
@tool
extends BTAction

@export var target_var: StringName = &"patrol_pos"
@export var speed_var: StringName = &"speed"
@export var tolerance: float = 20.0

func _generate_name() -> String:
    return "MoveTo %s" % [LimboUtility.decorate_var(target_var)]

func _tick(delta: float) -> Status:
    var target_pos: Vector2 = blackboard.get_var(target_var, Vector2.ZERO)
    if agent.global_position.distance_to(target_pos) < tolerance:
        return SUCCESS
    var speed: float = blackboard.get_var(speed_var, 100.0)
    var dir := agent.global_position.direction_to(target_pos)
    agent.velocity = dir * speed
    agent.move_and_slide()
    return RUNNING
```

---

## Pattern 2: Chase & Flee

### Chase BT
```
Sequence
├── BTCheckVar (check "target" is valid)
├── InRange (condition: target within chase range)
├── FaceTarget
└── MoveToward "target"
```

### Flee BT
```
Sequence
├── BTCheckVar (check "threat" is valid)
├── SelectFleePosition (away from threat)
└── MoveToPosition "flee_pos"
```

**SelectFleePosition**:
```gdscript
@tool
extends BTAction

@export var threat_var: StringName = &"threat"
@export var flee_pos_var: StringName = &"flee_pos"
@export var flee_distance: float = 300.0

func _tick(_delta: float) -> Status:
    var threat = blackboard.get_var(threat_var)
    if not is_instance_valid(threat):
        return FAILURE
    var away_dir: Vector2 = (agent.global_position - threat.global_position).normalized()
    blackboard.set_var(flee_pos_var, agent.global_position + away_dir * flee_distance)
    return SUCCESS
```

---

## Pattern 3: Ranged Combat with Positioning

```
Selector
├── Sequence [Attack if in range and can shoot]
│   ├── InRange (min: 100, max: 400, target_var: "target")
│   ├── BTCooldown (duration: 1.5)
│   │   └── Sequence
│   │       ├── FaceTarget
│   │       ├── BTPlayAnimation "shoot"
│   │       └── BTCallMethod "fire_projectile"
│   └── BTAlwaysSucceed
│       └── StrafeAroundTarget
├── Sequence [Get into range]
│   ├── InRange (min: 0, max: 100)  [too close]
│   └── BackAway
└── Sequence [Approach]
    └── MoveToward "target"
```

The `BTCooldown` decorator prevents shooting every frame. `BTAlwaysSucceed` wraps strafing so the Sequence doesn't fail if strafing can't execute.

---

## Pattern 4: Melee Combo Attacks

```
Selector
├── Sequence [Combo attack when close]
│   ├── InRange (max: 80)
│   ├── FaceTarget
│   └── BTRepeat (times: 3, abort_on_failure: true)
│       └── Sequence
│           ├── BTPlayAnimation "attack" (await_completion: 1.0)
│           └── BTWait (0.1)  [small gap between hits]
├── Sequence [Approach]
│   ├── InRange (max: 300)
│   └── MoveToward "target"
└── Idle
```

---

## Pattern 5: Group Coordination (Shared Blackboard)

Multiple agents share a parent blackboard for coordination:

```gdscript
# Squad manager script
class_name SquadManager
extends Node2D

@export var blackboard_plan: BlackboardPlan

var shared_bb: Blackboard

func _ready() -> void:
    shared_bb = blackboard_plan.create_blackboard() if blackboard_plan else Blackboard.new()
    for child in get_children():
        var bt_player: BTPlayer = child.find_child("BTPlayer")
        if bt_player:
            bt_player.blackboard.set_parent(shared_bb)
```

Tasks can then coordinate:
```gdscript
# In a custom task — claim an attack slot
func _tick(_delta: float) -> Status:
    var attackers: int = blackboard.top().get_var(&"active_attackers", 0)
    if attackers >= 2:  # max 2 agents attack simultaneously
        return FAILURE
    blackboard.top().set_var(&"active_attackers", attackers + 1)
    return SUCCESS

func _exit() -> void:
    var attackers: int = blackboard.top().get_var(&"active_attackers", 0)
    blackboard.top().set_var(&"active_attackers", maxi(0, attackers - 1))
```

---

## Pattern 6: Boss Phase Transitions (HSM + BT)

Use HSM for phase management, BT for behavior within each phase:

```gdscript
func _ready() -> void:
    var hsm := LimboHSM.new()
    add_child(hsm)

    # Phase 1: Aggressive melee
    var phase1 := BTState.new()
    phase1.behavior_tree = preload("res://ai/trees/boss_phase1.tres")
    phase1.failure_event = &"phase1_done"

    # Phase 2: Ranged + summons (triggers at 50% HP)
    var phase2 := BTState.new()
    phase2.behavior_tree = preload("res://ai/trees/boss_phase2.tres")
    phase2.failure_event = &"phase2_done"

    # Enrage: Final stand
    var enrage := BTState.new()
    enrage.behavior_tree = preload("res://ai/trees/boss_enrage.tres")

    hsm.add_child(phase1)
    hsm.add_child(phase2)
    hsm.add_child(enrage)

    hsm.add_transition(phase1, phase2, &"phase1_done")
    hsm.add_transition(phase2, enrage, &"phase2_done")

    hsm.initialize(self)
    hsm.set_active(true)
```

Health check triggers phase transitions:
```gdscript
func _on_health_changed(new_hp: float, max_hp: float) -> void:
    var ratio := new_hp / max_hp
    if ratio <= 0.5:
        $LimboHSM.dispatch(&"phase1_done")
    if ratio <= 0.2:
        $LimboHSM.dispatch(&"phase2_done")
```

---

## Pattern 7: Cooldown-Based Abilities

```
Selector
├── BTCooldown (duration: 8.0)
│   └── Sequence [Special attack]
│       ├── BTCheckVar "can_special" (check: EQUAL, value: true)
│       ├── FaceTarget
│       └── BTCallMethod "use_special_attack"
├── BTCooldown (duration: 2.0)
│   └── Sequence [Normal attack]
│       ├── InRange (max: 80)
│       ├── FaceTarget
│       └── BTPlayAnimation "attack"
└── MoveToward "target"
```

The Selector tries the special attack first (gated by 8s cooldown), falls through to normal attack (2s cooldown), then defaults to approaching.

---

## Pattern 8: Perception System

Store perception results in the blackboard, query them in conditions:

```gdscript
# On the agent script — update blackboard with perception data
func _physics_process(delta: float) -> void:
    var nearest_enemy = _find_nearest_in_group("enemies")
    if nearest_enemy:
        $BTPlayer.blackboard.set_var(&"target", nearest_enemy)
        $BTPlayer.blackboard.set_var(&"target_distance",
            global_position.distance_to(nearest_enemy.global_position))
    else:
        $BTPlayer.blackboard.set_var(&"target", null)
```

Then BT tasks just check blackboard values — clean separation between perception and decision-making.

---

## Anti-Patterns to Avoid

1. **Don't put heavy computation in `_tick()`** — cache in `_enter()` or `_setup()`
2. **Don't access parent scope directly** — use variable mapping in BTState
3. **Don't make monolithic trees** — use `BTSubtree` to compose smaller, reusable trees
4. **Don't forget `@tool`** — without it, `_generate_name()` won't work in the editor
5. **Don't type-hint nullable object vars** — use `var obj = blackboard.get_var(...)` then `is_instance_valid(obj)`
