# CIP Module — Usage Guide

## Setup

CIPSubsystem is a Node. Add it as an **autoload** in Project Settings:

`Project > Project Settings > Autoload > Add`
- Path: Create a scene with a single `CIPSubsystem` node, or use a script that instantiates one.
- Name: `CIP`

Or add it programmatically:

```gdscript
func _ready():
    var cip = CIPSubsystem.new()
    cip.name = "CIP"
    get_tree().root.add_child(cip)
```

---

## Core Concepts

### Actions are shared references

A `CIPAction` represents a single gameplay command. **The same instance** must be used everywhere that refers to that action:

```gdscript
# actions.gd — singleton/autoload holding all action definitions
var move := CIPAction.new()
var jump := CIPAction.new()
var interact := CIPAction.new()

func _init():
    move.value_type = CIPAction.VALUE_VECTOR2
    jump.value_type = CIPAction.VALUE_BOOL
    interact.value_type = CIPAction.VALUE_BOOL
```

Alternatively, save actions as `.tres` resources and `preload()` them. Godot's resource cache ensures the same file always returns the same instance.

```gdscript
const JUMP_ACTION = preload("res://input/actions/jump.tres")
```

**Never** create two separate `CIPAction.new()` calls for the same logical action — they won't share state.

### Mappings own their triggers

Each `CIPActionMapping` must have **its own trigger instances**. Triggers like `CIPTriggerHold` and `CIPTriggerReleased` carry internal state (timers, press tracking). Sharing a trigger instance across mappings causes state contamination.

```gdscript
# CORRECT — each mapping gets its own trigger
var hold_w = CIPTriggerHold.new()
hold_w.hold_time = 1.0
mapping_w.triggers = [hold_w]

var hold_s = CIPTriggerHold.new()
hold_s.hold_time = 1.0
mapping_s.triggers = [hold_s]

# WRONG — shared trigger, timers bleed between mappings
var shared_hold = CIPTriggerHold.new()
mapping_w.triggers = [shared_hold]
mapping_s.triggers = [shared_hold]  # bug: same elapsed counter
```

`CIPTriggerPressed` is stateless and safe to share, but there's no benefit — just create new instances.

### Modifiers are stateless — safe to share

All built-in modifiers (`DeadZone`, `Negate`, `Scale`, `Swizzle`) carry no runtime state. Sharing them across mappings is fine.

---

## Typical Setup Pattern

```gdscript
extends Node

# Define actions (shared across the game)
var move_action := CIPAction.new()
var jump_action := CIPAction.new()

func _ready():
    move_action.value_type = CIPAction.VALUE_VECTOR2
    jump_action.value_type = CIPAction.VALUE_BOOL

    # Connect signals
    move_action.triggered.connect(_on_move)
    jump_action.started.connect(_on_jump)

    # Build context
    var ctx := CIPMappingContext.new()
    ctx.priority = 10

    # WASD → move
    ctx.mappings = [
        _map_key(KEY_W, move_action, Vector2(0, -1)),
        _map_key(KEY_S, move_action, Vector2(0, 1)),
        _map_key(KEY_A, move_action, Vector2(-1, 0)),
        _map_key(KEY_D, move_action, Vector2(1, 0)),
        _map_key(KEY_SPACE, jump_action),
    ]

    CIPSubsystem.get_singleton().add_mapping_context(ctx)

func _map_key(key: int, action: CIPAction, scale_vec := Vector2.ZERO) -> CIPActionMapping:
    var m := CIPActionMapping.new()
    m.action = action
    var ev := InputEventKey.new()
    ev.keycode = key
    m.input_event = ev
    m.triggers = [CIPTriggerPressed.new()]
    if scale_vec != Vector2.ZERO:
        var s := CIPModifierScale.new()
        s.scale = Vector3(scale_vec.x, scale_vec.y, 0)
        var sw := CIPModifierSwizzle.new()  # promotes 1D → Vector2 Y axis
        m.modifiers = [sw, s]
    return m

func _on_move(value: Variant):
    velocity = value * speed

func _on_jump(_value: Variant):
    jump()
```

---

## Analog Stick (Gamepad)

Analog inputs pass nearly raw values to the modifier chain. **You must add a `CIPModifierDeadZone`** to filter stick drift:

```gdscript
var stick_mapping := CIPActionMapping.new()
stick_mapping.action = move_action

var ev := InputEventJoypadMotion.new()
ev.axis = JOY_AXIS_LEFT_X  # or LEFT_Y
stick_mapping.input_event = ev

var dz := CIPModifierDeadZone.new()
dz.inner_threshold = 0.15
dz.outer_threshold = 0.95
stick_mapping.modifiers = [dz]
stick_mapping.triggers = [CIPTriggerPressed.new()]
```

Without a deadzone modifier, tiny stick drift will register as input.

---

## Contexts and Priority

Higher priority contexts mask lower ones when `consume_input = true`:

```gdscript
# Normal gameplay
var gameplay_ctx := CIPMappingContext.new()
gameplay_ctx.priority = 0

# UI/menu override — same keys, different behavior
var menu_ctx := CIPMappingContext.new()
menu_ctx.priority = 100  # higher wins

# Activate menu context → gameplay keys stop firing
CIPSubsystem.get_singleton().add_mapping_context(menu_ctx)

# Deactivate → gameplay resumes (active actions get CANCELED signal)
CIPSubsystem.get_singleton().remove_mapping_context(menu_ctx)
```

When you remove a context, any actions that were actively triggered from that context receive a `canceled` signal. This ensures listeners always get a clean termination.

---

## Trigger Types

| Trigger | Behavior | Use Case |
|---------|----------|----------|
| `CIPTriggerPressed` | Fires immediately when value exceeds threshold | Jump, fire, interact |
| `CIPTriggerReleased` | Fires on transition from pressed→released | Charge attacks (release to fire) |
| `CIPTriggerHold` | Fires after holding for N seconds | Hold to sprint, hold to pick up |
| *(none assigned)* | Fires immediately on any matched event | Simple pass-through for movement |

If a mapping has **no triggers**, it fires immediately when the event matches (with `strength > 0` → TRIGGERED, `strength == 0` → releases).

---

## Custom Modifiers & Triggers (GDScript)

```gdscript
# Custom modifier — clamp to cardinal directions
class_name CardinalModifier extends CIPModifier

func _modify(value: Variant, delta: float) -> Variant:
    var v: Vector2 = value
    if abs(v.x) > abs(v.y):
        return Vector2(sign(v.x), 0)
    else:
        return Vector2(0, sign(v.y))
```

```gdscript
# Custom trigger — double-tap detection
class_name DoubleTapTrigger extends CIPTrigger

var tap_window := 0.3
var last_tap_time := 0.0
var tap_count := 0

func _update_state(value: Variant, delta: float) -> int:
    if value > 0.5:
        var now := Time.get_ticks_msec() / 1000.0
        if now - last_tap_time < tap_window:
            tap_count += 1
        else:
            tap_count = 1
        last_tap_time = now
        if tap_count >= 2:
            tap_count = 0
            return CIPTrigger.STATE_TRIGGERED
        return CIPTrigger.STATE_ONGOING
    return CIPTrigger.STATE_NONE
```

---

## Signal Reference

All signals pass the current `value` (typed according to `CIPAction.value_type`):

| Signal | When |
|--------|------|
| `started(value)` | First frame of activation |
| `ongoing(value)` | Trigger is progressing but not yet complete (hold charging) |
| `triggered(value)` | Trigger condition fully met (sustained each frame) |
| `completed(value)` | Clean release after being triggered |
| `canceled(value)` | Interrupted (context removed, or fell to NONE from ONGOING) |

Lifecycle: `started` → (`ongoing`*) → `triggered`* → `completed`
Or: `started` → (`ongoing`*) → `canceled`

---

## Performance Notes

- All evaluation runs on the main thread, synchronously
- Event matching is O(contexts × mappings per context) per input event
- Tick evaluation is O(active actions with triggers) per frame
- For typical games (< 100 total mappings, < 20 active actions): < 0.1ms/frame
