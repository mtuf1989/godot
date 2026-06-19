# LimboAI Core Reference

## Table of Contents
- Core Concepts
- Task Lifecycle
- Custom Task Template
- Composites
- Decorators
- Blackboard
- HSM Setup
- BT + HSM Hybrid
- Performance
- Common Traps

## Core Concepts

LimboAI has three pillars:

1. **Behavior Trees** — hierarchical task graphs that tick every frame. A `BTPlayer` node runs a `BehaviorTree` resource. Tasks return `SUCCESS`, `FAILURE`, or `RUNNING`.
2. **Hierarchical State Machines** — event-driven `LimboHSM` with `LimboState` nodes. Transitions fire on named events. States have `_enter/_exit/_update` lifecycle.
3. **Blackboard** — shared key-value store. Scoped (parent chain lookup). Plans define schemas. `BBParam` types let task properties bind to either a literal value or a blackboard variable.

The bridge: `BTState` runs a BehaviorTree inside an HSM state — combine macro-level state management (idle/combat/flee) with micro-level BT decision-making.

## Task Lifecycle

Every task goes through: `FRESH` → `RUNNING` → `SUCCESS`/`FAILURE` → (back to `FRESH` on re-entry).

### Lifecycle Methods

```
_setup()     → Called ONCE during tree initialization (before any execution)
_enter()     → Called when the task starts executing
_tick(delta) → Called every frame while active (MUST return a Status)
_exit()      → Called when the task finishes or is aborted
```

### Execution Flow Per Frame

When `execute(delta)` is called on a task:
1. If status != RUNNING and status != FRESH → **abort() all children** (reset to FRESH)
2. If status != RUNNING → call `_enter()`
3. If status == RUNNING → `elapsed_time += delta`
4. Call `_tick(delta)` → must return SUCCESS, FAILURE, or RUNNING
5. If status != RUNNING after tick → call `_exit()`, reset `elapsed_time` to 0

### Critical Call Order (Asymmetric)

- **`_enter()`**: native first, then script — script can build on native setup
- **`_exit()`**: script first, then native — script cleans up before native teardown
- **`_tick()`**: script completely overrides native if defined

### Re-entry Behavior

When a task that previously returned SUCCESS or FAILURE is executed again, ALL its children are `abort()`ed first — their state is wiped clean. `abort()` cascades depth-first: recursively aborts children, calls `_exit()` only on RUNNING tasks, then resets status to FRESH.

### Timing

`elapsed_time` is **0 on the first tick** because delta only accumulates when status is already RUNNING before the tick. Don't rely on `elapsed_time` for first-tick logic.

## Task Hierarchy — What to Extend

| Base Class | When to Use | Returns |
|---|---|---|
| `BTAction` | Leaf that *does* something (move, attack, set variable) | SUCCESS/FAILURE/RUNNING |
| `BTCondition` | Leaf that *checks* something (in range? health low?) | SUCCESS/FAILURE |
| `BTDecorator` | Wraps one child, modifies its behavior | Depends on logic |
| `BTComposite` | Controls flow of multiple children | Depends on logic |

Most custom work = writing `BTAction` and `BTCondition` scripts.

## Custom Task Template

```gdscript
@tool
extends BTAction  # or BTCondition

## Document what this task does.
## Document return values.

@export var my_var: StringName = &"target"  # BB variable names end with _var
@export var speed: float = 200.0            # Direct parameters

func _generate_name() -> String:
    return "MyTask %s" % [LimboUtility.decorate_var(my_var)]

func _setup() -> void:
    pass

func _enter() -> void:
    pass

func _exit() -> void:
    pass

func _tick(delta: float) -> Status:
    var target: Node2D = blackboard.get_var(my_var, null)
    if not is_instance_valid(target):
        return FAILURE
    # ... do work ...
    return SUCCESS  # or FAILURE or RUNNING
```

Key rules:
- Always use `@tool` if you define `_generate_name()` — it runs in the editor
- Access the agent node via `agent` (the node the BTPlayer is attached to or its agent_node property)
- Access the scene root via `scene_root`
- Access shared data via `blackboard.get_var()` / `blackboard.set_var()`
- Suffix blackboard variable name properties with `_var` — enables the inspector's variable picker
- Use `LimboUtility.decorate_var(var_name)` in `_generate_name()` to format variable references
- For nullable object variables, don't type-hint the `var` to avoid errors on freed instances
- Use `is_instance_valid()` before accessing object variables from the blackboard

## Composites

| Composite | Behavior | Use When |
|---|---|---|
| `BTSequence` | Run children L→R, fail on first failure | "Do A, then B, then C" (all must succeed) |
| `BTSelector` | Run children L→R, succeed on first success | "Try A, else try B, else try C" |
| `BTParallel` | Run all children simultaneously | "Do A and B at the same time" |
| `BTDynamicSequence` | Re-evaluate from start each tick | Earlier conditions may invalidate later work |
| `BTDynamicSelector` | Re-evaluate from start each tick | Higher-priority options should preempt |
| `BTRandomSelector/Sequence` | Shuffled order | Add variety to behavior |
| `BTProbabilitySelector` | Weighted random | "70% attack, 20% dodge, 10% taunt" |

## Decorators

| Decorator | Effect |
|---|---|
| `BTRepeat` | Repeat child N times (or forever with `forever=true`) |
| `BTInvert` | Flip SUCCESS↔FAILURE |
| `BTAlwaysSucceed/Fail` | Override child result |
| `BTCooldown` | Block re-execution for N seconds |
| `BTDelay` | Wait N seconds before running child |
| `BTTimeLimit` | Abort child after N seconds |
| `BTRunLimit` | Limit total number of executions |
| `BTForEach` | Iterate over array blackboard variable |
| `BTProbability` | Run child with random chance (0.0–1.0) |
| `BTSubtree` | Execute external BehaviorTree resource |
| `BTNewScope` | Create isolated blackboard scope for child |

## Blackboard Best Practices

1. **Define variables in the BlackboardPlan** — gives type safety, defaults, and editor support
2. **Use BBParam types** for task properties that should be bindable to BB variables:
   ```gdscript
   @export var speed: BBFloat
   func _tick(delta: float) -> Status:
       var s: float = speed.get_value(scene_root, blackboard, 100.0)
   ```
3. **Scope awareness**: `get_var()` searches up the parent chain; `set_var()` writes locally; `top()` gets the root scope
4. **Sharing between agents**: Set a common parent blackboard for group coordination
5. **Variable mapping** in BTState: Map BT variables to HSM variables explicitly in the inspector rather than relying on parent scope access

## HSM Setup

### Scene tree approach (states as child nodes):
```gdscript
@onready var hsm: LimboHSM = $LimboHSM
@onready var idle_state: LimboState = $LimboHSM/IdleState
@onready var combat_state: LimboState = $LimboHSM/CombatState

func _ready() -> void:
    hsm.add_transition(idle_state, combat_state, &"enemy_spotted")
    hsm.add_transition(combat_state, idle_state, &"enemy_lost")
    hsm.add_transition(hsm.ANYSTATE, idle_state, &"reset")
    hsm.initialize(self)
    hsm.set_active(true)
```

### Single-file approach (rapid prototyping):
```gdscript
var hsm: LimboHSM

func _ready() -> void:
    hsm = LimboHSM.new()
    add_child(hsm)

    var idle := LimboState.new().named("Idle") \
        .call_on_enter(func(): $AnimationPlayer.play("idle")) \
        .call_on_update(_idle_update)
    var chase := LimboState.new().named("Chase") \
        .call_on_enter(func(): $AnimationPlayer.play("run")) \
        .call_on_update(_chase_update)

    hsm.add_child(idle)
    hsm.add_child(chase)
    hsm.add_transition(idle, chase, &"enemy_spotted")
    hsm.add_transition(chase, idle, &"enemy_lost")
    hsm.initialize(self)
    hsm.set_active(true)
```

## BT + HSM Hybrid

```gdscript
var hsm: LimboHSM

func _ready() -> void:
    hsm = LimboHSM.new()
    add_child(hsm)

    var idle := LimboState.new().named("Idle")
    var combat := BTState.new()
    combat.behavior_tree = preload("res://ai/trees/combat.tres")
    combat.success_event = &"combat_won"
    combat.failure_event = &"combat_lost"

    hsm.add_child(idle)
    hsm.add_child(combat)
    hsm.add_transition(idle, combat, &"enemy_spotted")
    hsm.add_transition(combat, idle, &"combat_won")
    hsm.add_transition(combat, idle, &"combat_lost")
    hsm.initialize(self)
    hsm.set_active(true)
```

### BTPlayer Initialization Timing

BTPlayer does NOT initialize during `_ready()`. It defers until the scene root propagates ready. This means `bt_player.get_bt_instance()` returns `null` in `_ready()`:

```gdscript
# WRONG — bt_instance is null here:
func _ready():
    var instance = $BTPlayer.get_bt_instance()  # null!

# RIGHT — use the updated signal or call_deferred:
func _ready():
    $BTPlayer.updated.connect(_on_bt_updated)
```

Variables set on the blackboard before initialization are preserved (`populate_blackboard` uses `overwrite=false`).

### Dynamic BTState/BTPlayer Creation

When creating BTState or BTPlayer programmatically, you MUST call `set_scene_root_hint()` before adding to the tree — otherwise scene root detection fails:

```gdscript
var bt_state = BTState.new()
bt_state.behavior_tree = preload("res://ai/behavior.tres")
bt_state.set_scene_root_hint(self)  # CRITICAL — before add_child
hsm.add_child(bt_state)
```

## Performance

- Prefer `distance_squared_to()` over `distance_to()` in conditions — avoids sqrt
- Cache expensive lookups in `_setup()` or `_enter()`, not `_tick()`
- Use `BTCooldown` to throttle expensive checks
- `BTDynamicSelector/Sequence` re-evaluate from the start every tick — powerful but costlier than static versions
- For many agents, consider `BTPlayer.update_mode = PHYSICS` and stagger updates if needed

## File Organization

```
res://ai/
├── tasks/           # Custom BTAction/BTCondition scripts
│   ├── actions/     # Movement, attacks, etc.
│   └── conditions/  # Range checks, health checks, etc.
└── trees/           # .tres BehaviorTree resources
```

Place custom tasks in `res://ai/tasks` (configurable in Project Settings → LimboAI). Subdirectories become categories in the Task Palette.

## Common Traps

These are the most frequent sources of subtle bugs that compile fine but fail at runtime.

### Blackboard

| Trap | Why It Matters |
|------|----------------|
| **`set_var()` never modifies parent scope** | Always writes to local scope. If parent has "x" and you `set_var("x", val)` on a child, you create a shadow copy — parent's value is unchanged. This is the #1 "my variable isn't updating" bug. |
| **`restart()` doesn't reset blackboard** | Only resets task states to FRESH. All blackboard variables persist. Clear them manually if needed. |
| **`BTCheckTrigger` auto-resets** | Returns SUCCESS when variable is `true`, then automatically sets it to `false`. Non-bool values always return FAILURE. |
| **`BTSetVar` arithmetic on missing variable** | Performing ADD/SUBTRACT on a non-existent variable returns FAILURE, not an error. |

### BTPlayer

| Trap | Why It Matters |
|------|----------------|
| **`get_bt_instance()` is null during `_ready()`** | Initialization is deferred until scene root propagates ready. Use the `updated` signal or `call_deferred`. |
| **`set_scene_root_hint()` required for dynamic creation** | Without it, BTPlayer/BTState can't detect scene root and initialization fails with "Failed to detect scene root". |
| **Setting `agent_node` after instantiation has no effect** | Must be set before the node enters the tree. |

### HSM

| Trap | Why It Matters |
|------|----------------|
| **Event handlers MUST return bool** | Non-bool returns from handler callables print errors. Return `true` to consume the event, `false` to propagate. |
| **Guard callables MUST return bool** | Same requirement. Non-bool returns cause errors. |
| **ANYSTATE doesn't self-transition** | If the target state is already active, the transition is silently ignored. To restart the current state, use a specific (non-ANYSTATE) transition or `change_active_state()`. |
| **`EVENT_FINISHED` deactivates root HSM** | If unhandled at root level, the entire HSM exits and becomes inactive. |
| **`initialize()` and `set_active()` only on root HSM** | Calling on nested HSMs prints an error. The root recursively initializes children. |
| **Deferred transitions: only first wins** | During `_update()`, only the first triggered transition is honored per tick. |

### Tasks

| Trap | Why It Matters |
|------|----------------|
| **Decorator with no child = FAILURE** | Every decorator returns FAILURE immediately if it has no child. Most common "why isn't my tree working" issue. |
| **Re-entry aborts all children** | When a non-FRESH, non-RUNNING task re-executes, all children are aborted first. State is wiped between parent re-entries. |
| **`_generate_name()` requires `@tool`** | Without `@tool`, you get a runtime error. Enforced even in editor. |
| **Empty BTSequence returns SUCCESS** | A Sequence with no children returns SUCCESS, not FAILURE. |

### Thread Safety

LimboAI is NOT thread-safe. No mutexes or locks anywhere. All BT and HSM operations must run on the main thread.
