# CIP (Contextual Input Pipeline) — Implementation Guide

## Overview

A Godot 4 engine module that adds Unreal-style data-driven input contexts with prioritized stacking, extensible modifier/trigger pipelines, and event-driven action signals.

**Location:** `modules/cip/`

---

## Architecture

```
[Physical InputEvent from OS]
         │
         ▼
┌─────────────────────────────────┐
│ Godot Viewport::push_input()    │
│  1. _input group                │
│  2. _gui_input_event (UI)       │
│  3. _push_unhandled_input:      │
│     - shortcut_input            │
│     - unhandled_key_input       │
│     - _unhandled_input ← CIP   │
└─────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────┐
│ CIPSubsystem._unhandled_input() │
│  For each context (by priority):│
│    For each mapping in context: │
│      1. action_match(event)     │
│      2. Run modifier chain      │
│      3. Run trigger evaluation  │
│      4. Emit signals on action  │
│      5. Consume if matched      │
└─────────────────────────────────┘
```

---

## Classes to Implement (Phase 1)

### 1. CIPAction (Resource)
Abstract gameplay command ("jump", "move", "interact").

```cpp
// modules/cip/cip_action.h
#pragma once
#include "core/io/resource.h"

class CIPAction : public Resource {
    GDCLASS(CIPAction, Resource);

public:
    enum ValueType {
        VALUE_BOOL,
        VALUE_FLOAT,
        VALUE_VECTOR2,
        VALUE_VECTOR3
    };

    enum TriggerEvent {
        TRIGGER_NONE,
        TRIGGER_STARTED,
        TRIGGER_ONGOING,
        TRIGGER_TRIGGERED,
        TRIGGER_COMPLETED,
        TRIGGER_CANCELED
    };

private:
    ValueType value_type = VALUE_BOOL;
    Variant current_value;
    TriggerEvent current_state = TRIGGER_NONE;

protected:
    static void _bind_methods();

public:
    void set_value_type(ValueType p_type);
    ValueType get_value_type() const;

    Variant get_value() const;
    TriggerEvent get_trigger_event() const;

    // Called by CIPSubsystem during evaluation
    void _update(const Variant &p_value, TriggerEvent p_event);
};
```

**Signals:** `started`, `ongoing(value)`, `triggered(value)`, `completed`, `canceled`

---

### 2. CIPModifier (Resource)
Pre-processor that transforms raw input values. User-overridable via GDScript.

```cpp
// modules/cip/cip_modifier.h
#pragma once
#include "core/io/resource.h"

class CIPModifier : public Resource {
    GDCLASS(CIPModifier, Resource);

protected:
    static void _bind_methods();
    GDVIRTUAL2RC(Variant, _modify, Variant, double) // (current_value, delta) -> modified_value

public:
    Variant modify(const Variant &p_value, double p_delta) const;
};
```

**Built-in subclasses for Phase 1:**
- `CIPModifierDeadZone` — radial deadzone with inner/outer thresholds
- `CIPModifierNegate` — invert sign
- `CIPModifierScale` — multiply by factor
- `CIPModifierSwizzle` — remap axis components (e.g., 1D key → Y axis of Vector2)

---

### 3. CIPTrigger (Resource)
Evaluates post-modifier values to determine action state. User-overridable.

```cpp
// modules/cip/cip_trigger.h
#pragma once
#include "core/io/resource.h"

class CIPTrigger : public Resource {
    GDCLASS(CIPTrigger, Resource);

public:
    enum TriggerState {
        STATE_NONE,
        STATE_ONGOING,
        STATE_TRIGGERED
    };

protected:
    static void _bind_methods();
    GDVIRTUAL2RC(int, _update_state, Variant, double) // (modified_value, delta) -> TriggerState

public:
    TriggerState update_state(const Variant &p_value, double p_delta);
};
```

**Built-in subclasses for Phase 1:**
- `CIPTriggerPressed` — triggers on press (bool true or strength > 0)
- `CIPTriggerReleased` — triggers on release
- `CIPTriggerHold` — requires holding for N seconds (uses delta accumulation)

---

### 4. CIPActionMapping (Resource)
Links a physical event to an action through modifiers and triggers.

```cpp
// modules/cip/cip_action_mapping.h
#pragma once
#include "core/io/resource.h"
#include "core/input/input_event.h"
#include "cip_action.h"
#include "cip_modifier.h"
#include "cip_trigger.h"

class CIPActionMapping : public Resource {
    GDCLASS(CIPActionMapping, Resource);

private:
    Ref<CIPAction> action;
    Ref<InputEvent> input_event;   // The physical event template to match against
    TypedArray<CIPModifier> modifiers;
    TypedArray<CIPTrigger> triggers;
    bool consume_input = true;

protected:
    static void _bind_methods();

public:
    void set_action(const Ref<CIPAction> &p_action);
    Ref<CIPAction> get_action() const;

    void set_input_event(const Ref<InputEvent> &p_event);
    Ref<InputEvent> get_input_event() const;

    void set_modifiers(const TypedArray<CIPModifier> &p_modifiers);
    TypedArray<CIPModifier> get_modifiers() const;

    void set_triggers(const TypedArray<CIPTrigger> &p_triggers);
    TypedArray<CIPTrigger> get_triggers() const;

    void set_consume_input(bool p_consume);
    bool get_consume_input() const;
};
```

---

### 5. CIPMappingContext (Resource)
A prioritized collection of action mappings.

```cpp
// modules/cip/cip_mapping_context.h
#pragma once
#include "core/io/resource.h"
#include "cip_action_mapping.h"

class CIPMappingContext : public Resource {
    GDCLASS(CIPMappingContext, Resource);

private:
    int priority = 0;
    TypedArray<CIPActionMapping> mappings;

protected:
    static void _bind_methods();

public:
    void set_priority(int p_priority);
    int get_priority() const;

    void set_mappings(const TypedArray<CIPActionMapping> &p_mappings);
    TypedArray<CIPActionMapping> get_mappings() const;
};
```

---

### 6. CIPSubsystem (Node)
Singleton orchestrator. Registered as autoload.

```cpp
// modules/cip/cip_subsystem.h
#pragma once
#include "scene/main/node.h"
#include "cip_mapping_context.h"

class CIPSubsystem : public Node {
    GDCLASS(CIPSubsystem, Node);

private:
    static CIPSubsystem *singleton;
    Vector<Ref<CIPMappingContext>> active_contexts; // sorted by priority desc

    // Per-action runtime state tracking
    struct ActionState {
        CIPTrigger::TriggerState last_state = CIPTrigger::STATE_NONE;
        Variant last_value;
    };
    HashMap<Ref<CIPAction>, ActionState> action_states;

    void _sort_contexts();

protected:
    static void _bind_methods();
    void _unhandled_input(const Ref<InputEvent> &p_event);
    void _notification(int p_what);

public:
    static CIPSubsystem *get_singleton();

    void add_mapping_context(const Ref<CIPMappingContext> &p_context);
    void remove_mapping_context(const Ref<CIPMappingContext> &p_context);
    void clear_mapping_contexts();

    CIPSubsystem();
    ~CIPSubsystem();
};
```

---

## Module Boilerplate

### config.py
```python
def can_build(env, platform):
    return True

def configure(env):
    pass

def get_doc_classes():
    return [
        "CIPAction",
        "CIPModifier",
        "CIPModifierDeadZone",
        "CIPModifierNegate",
        "CIPModifierScale",
        "CIPModifierSwizzle",
        "CIPTrigger",
        "CIPTriggerPressed",
        "CIPTriggerReleased",
        "CIPTriggerHold",
        "CIPActionMapping",
        "CIPMappingContext",
        "CIPSubsystem",
    ]

def get_doc_path():
    return "doc_classes"
```

### SCsub
```python
Import("env")
env.add_source_files(env.modules_sources, "*.cpp")
```

### register_types.h / register_types.cpp
Standard module registration pattern (see modules/noise/ for reference).

---

## Key Implementation Details

### Event Matching (Item 3 Resolution)

```cpp
// In CIPSubsystem::_unhandled_input
void CIPSubsystem::_unhandled_input(const Ref<InputEvent> &p_event) {
    double delta = get_process_delta_time();
    HashSet<Ref<CIPAction>> consumed_actions;

    for (const Ref<CIPMappingContext> &context : active_contexts) {
        TypedArray<CIPActionMapping> mappings = context->get_mappings();

        for (int i = 0; i < mappings.size(); i++) {
            Ref<CIPActionMapping> mapping = mappings[i];
            Ref<CIPAction> action = mapping->get_action();

            if (action.is_null() || consumed_actions.has(action)) continue;

            Ref<InputEvent> registered = mapping->get_input_event();
            if (registered.is_null()) continue;

            // Match physical event against stored template
            bool pressed = false;
            float strength = 0.0f;
            float raw_strength = 0.0f;

            // Device matching
            int reg_device = registered->get_device();
            if (reg_device != InputMap::ALL_DEVICES && reg_device != p_event->get_device()) continue;

            bool matched = registered->action_match(
                p_event,
                false,           // allow superset modifiers
                0.5f,            // default deadzone (can come from action later)
                &pressed,
                &strength,
                &raw_strength
            );

            if (!matched) continue;

            // Run modifier chain
            Variant value = Variant(strength); // or Vector2/Vector3 depending on action type
            TypedArray<CIPModifier> modifiers = mapping->get_modifiers();
            for (int m = 0; m < modifiers.size(); m++) {
                Ref<CIPModifier> mod = modifiers[m];
                if (mod.is_valid()) {
                    value = mod->modify(value, delta);
                }
            }

            // Run trigger evaluation
            CIPTrigger::TriggerState trigger_result = CIPTrigger::STATE_TRIGGERED; // default if no triggers
            TypedArray<CIPTrigger> triggers = mapping->get_triggers();
            for (int t = 0; t < triggers.size(); t++) {
                Ref<CIPTrigger> trigger = triggers[t];
                if (trigger.is_valid()) {
                    CIPTrigger::TriggerState result = trigger->update_state(value, delta);
                    if ((int)result < (int)trigger_result) {
                        trigger_result = result;
                    }
                }
            }

            // State transition logic & signal emission
            ActionState &state = action_states[action];
            CIPTrigger::TriggerState prev = state.last_state;
            state.last_state = trigger_result;
            state.last_value = value;

            action->_update(value, _compute_trigger_event(prev, trigger_result));

            // Consume input for this action (higher priority wins)
            if (mapping->get_consume_input()) {
                consumed_actions.insert(action);
            }
        }
    }

    // If any action consumed, mark input as handled
    if (!consumed_actions.is_empty()) {
        get_viewport()->set_input_as_handled();
    }
}
```

### State Transition Logic

```cpp
CIPAction::TriggerEvent CIPSubsystem::_compute_trigger_event(
    CIPTrigger::TriggerState p_prev,
    CIPTrigger::TriggerState p_current
) {
    if (p_current == CIPTrigger::STATE_TRIGGERED) {
        if (p_prev != CIPTrigger::STATE_TRIGGERED) {
            return CIPAction::TRIGGER_STARTED; // also emit triggered
        }
        return CIPAction::TRIGGER_TRIGGERED;
    } else if (p_current == CIPTrigger::STATE_ONGOING) {
        if (p_prev == CIPTrigger::STATE_NONE) {
            return CIPAction::TRIGGER_STARTED;
        }
        return CIPAction::TRIGGER_ONGOING;
    } else { // STATE_NONE
        if (p_prev == CIPTrigger::STATE_ONGOING) {
            return CIPAction::TRIGGER_CANCELED;
        } else if (p_prev == CIPTrigger::STATE_TRIGGERED) {
            return CIPAction::TRIGGER_COMPLETED;
        }
        return CIPAction::TRIGGER_NONE;
    }
}
```

### GDVIRTUAL Pattern (Item 1 Resolution)

```cpp
// cip_modifier.cpp
Variant CIPModifier::modify(const Variant &p_value, double p_delta) const {
    Variant ret = p_value; // default: pass-through
    GDVIRTUAL_CALL(_modify, p_value, p_delta, ret);
    return ret;
}

void CIPModifier::_bind_methods() {
    ClassDB::bind_method(D_METHOD("modify", "value", "delta"), &CIPModifier::modify);
    GDVIRTUAL_BIND(_modify, "current_value", "delta")
}
```

### Resource Serialization (Item 4 Resolution)

```cpp
// cip_mapping_context.cpp
void CIPMappingContext::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_priority", "priority"), &CIPMappingContext::set_priority);
    ClassDB::bind_method(D_METHOD("get_priority"), &CIPMappingContext::get_priority);
    ClassDB::bind_method(D_METHOD("set_mappings", "mappings"), &CIPMappingContext::set_mappings);
    ClassDB::bind_method(D_METHOD("get_mappings"), &CIPMappingContext::get_mappings);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "mappings", PROPERTY_HINT_ARRAY_TYPE,
            MAKE_RESOURCE_TYPE_HINT("CIPActionMapping")),
        "set_mappings", "get_mappings"
    );
}
```

### Execution Timing (Item 2 Resolution)

- **When input arrives:** OS → `Input::parse_input_event()` → buffer
- **When buffer flushes:** Each physics step (if agile) + before `_process()`  
  - `main.cpp:4970` — before each physics step
  - `main.cpp:5052` — before `_process`
- **Our hook:** `_unhandled_input` fires once per event after GUI processing
- **Performance:** Synchronous on main thread. <0.1ms for typical game (< 100 actions)

---

## Built-in Modifier Implementations

### CIPModifierDeadZone
```cpp
class CIPModifierDeadZone : public CIPModifier {
    GDCLASS(CIPModifierDeadZone, CIPModifier);
    float inner_threshold = 0.2f;
    float outer_threshold = 1.0f;

    // Override _modify: if |value| < inner → 0, else remap [inner, outer] → [0, 1]
    // For Vector2: apply radially (normalize, apply to magnitude, reconstruct)
};
```

### CIPModifierNegate
```cpp
class CIPModifierNegate : public CIPModifier {
    GDCLASS(CIPModifierNegate, CIPModifier);
    // _modify: return -value (works for float, Vector2, Vector3)
};
```

### CIPModifierScale
```cpp
class CIPModifierScale : public CIPModifier {
    GDCLASS(CIPModifierScale, CIPModifier);
    Vector3 scale = Vector3(1, 1, 1); // per-axis scale factor
    // _modify: return value * scale (component-wise)
};
```

### CIPModifierSwizzle
```cpp
class CIPModifierSwizzle : public CIPModifier {
    GDCLASS(CIPModifierSwizzle, CIPModifier);
    enum SwizzleOrder { YXZ, ZYX, XZY, YZX, ZXY };
    // _modify: reorder components of Vector2/Vector3
    // Also handles promoting 1D → specific axis of Vector2/Vector3
};
```

---

## Built-in Trigger Implementations

### CIPTriggerPressed
```cpp
class CIPTriggerPressed : public CIPTrigger {
    GDCLASS(CIPTriggerPressed, CIPTrigger);
    float actuation_threshold = 0.5f;
    // _update_state: value > threshold → TRIGGERED, else NONE
};
```

### CIPTriggerReleased
```cpp
class CIPTriggerReleased : public CIPTrigger {
    GDCLASS(CIPTriggerReleased, CIPTrigger);
    float actuation_threshold = 0.5f;
    // _update_state: value < threshold AND was previously above → TRIGGERED, else NONE
    // Needs internal state tracking (was_pressed)
};
```

### CIPTriggerHold
```cpp
class CIPTriggerHold : public CIPTrigger {
    GDCLASS(CIPTriggerHold, CIPTrigger);
    float hold_time = 0.5f;  // seconds
    float elapsed = 0.0f;
    bool is_first_trigger = true; // trigger once or continuously
    // _update_state:
    //   if value active: elapsed += delta
    //     if elapsed >= hold_time → TRIGGERED
    //     else → ONGOING
    //   else: elapsed = 0 → NONE
};
```

---

## File Structure

```
modules/cip/
├── config.py
├── SCsub
├── register_types.h
├── register_types.cpp
├── cip_action.h
├── cip_action.cpp
├── cip_modifier.h
├── cip_modifier.cpp
├── cip_modifier_dead_zone.h
├── cip_modifier_dead_zone.cpp
├── cip_modifier_negate.h
├── cip_modifier_negate.cpp
├── cip_modifier_scale.h
├── cip_modifier_scale.cpp
├── cip_modifier_swizzle.h
├── cip_modifier_swizzle.cpp
├── cip_trigger.h
├── cip_trigger.cpp
├── cip_trigger_pressed.h
├── cip_trigger_pressed.cpp
├── cip_trigger_released.h
├── cip_trigger_released.cpp
├── cip_trigger_hold.h
├── cip_trigger_hold.cpp
├── cip_action_mapping.h
├── cip_action_mapping.cpp
├── cip_mapping_context.h
├── cip_mapping_context.cpp
├── cip_subsystem.h
├── cip_subsystem.cpp
└── doc_classes/
    ├── CIPAction.xml
    ├── CIPModifier.xml
    └── ... (one per class)
```

---

## GDScript Usage Example (What We're Building Toward)

```gdscript
# Setup in _ready()
var move_action = CIPAction.new()
move_action.value_type = CIPAction.VALUE_VECTOR2

var jump_action = CIPAction.new()
jump_action.value_type = CIPAction.VALUE_BOOL

# Connect signals
move_action.triggered.connect(_on_move)
jump_action.triggered.connect(_on_jump)

# Create context
var ctx = CIPMappingContext.new()
ctx.priority = 10

# Map W key → move action Y+
var w_mapping = CIPActionMapping.new()
w_mapping.action = move_action
w_mapping.input_event = preload("res://input/key_w.tres") # InputEventKey
w_mapping.modifiers = [CIPModifierSwizzle.new()]  # 1D → Y axis
ctx.mappings = [w_mapping]

# Activate
CIPSubsystem.get_singleton().add_mapping_context(ctx)
```

---

## Key Source Code References

| What | Where |
|------|-------|
| InputMap singleton (what we're augmenting) | `core/input/input_map.h` |
| Input event matching (`action_match`) | `core/input/input_event.cpp:564` (Key), `:1133` (JoypadMotion) |
| Viewport input chain | `scene/main/viewport.cpp:3489` (`push_input`), `:3620` (`_push_unhandled_input_internal`) |
| SceneTree group propagation | `scene/main/scene_tree.cpp:1430` (`_call_input_pause`) |
| Main loop flush timing | `main/main.cpp:4970` (physics), `:5052` (process) |
| Module registration pattern | `modules/noise/register_types.cpp` |
| TypedArray<Resource> property | `scene/resources/font.cpp:108`, `scene/resources/compositor.cpp:213` |
| GDVIRTUAL on Resource | `scene/resources/texture.h:48`, `scene/resources/style_box.h:49` |
| MAKE_RESOURCE_TYPE_HINT macro | `core/object/object.h:68` |
| ERR_MAIN_THREAD_GUARD | `scene/main/node.h:937` |
| WorkerThreadPool (v2 reference) | `core/object/worker_thread_pool.h` |

---

## Build Command

```bash
scons platform=macos target=editor module_cip_enabled=yes
```

---

## Verification Plan

1. Build with module → no compile errors
2. Create CIPAction, CIPMappingContext resources in editor → inspector shows properties
3. Write GDScript test: activate context, press mapped key → signal fires
4. Test priority: two contexts map same key → higher priority wins
5. Test modifiers: deadzone removes drift, scale amplifies
6. Test triggers: hold trigger requires sustained input

---

## Design Decisions Log

| Decision | Rationale |
|----------|-----------|
| Engine module, not GDExtension | Full API access, no godot-cpp binding quirks, source already checked out |
| Synchronous main-thread evaluation | ERR_MAIN_THREAD_GUARD prevents threading; performance is fine for typical games |
| `_unhandled_input` hook | Fires after GUI, correct for gameplay actions; matches document's design |
| `action_match()` for event comparison | Handles all event types, outputs strength/pressed, applies deadzone |
| TypedArray for mappings | Simpler than count-based; auto-serializes inline; good editor UI |
| GDVIRTUAL for modifiers/triggers | Allows GDScript users to create custom modifiers without C++ |
| Signals for action events | Idiomatic Godot; decouples input from gameplay code |
| Consumed actions tracked per-frame | Higher priority context masks lower ones for same action |
