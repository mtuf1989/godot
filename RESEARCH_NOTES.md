# Research Notes: Verifying "Godot Input Architecture Blueprint"

## Document Summary
The document proposes a C++ GDExtension ("CIP - Contextual Input Pipeline") for Godot 4 that adds:
- Data-driven input mapping contexts (like Unreal's EnhancedInput)
- Prioritized context stacking with input consumption
- Extensible modifier/trigger pipeline
- Multi-threaded input evaluation via WorkerThreadPool

---

## Hypothesis Tree & Findings

### H1: Godot InputMap is a global static registry with string-to-event mappings
- **Status**: ✅ CONFIRMED
- **Confidence**: 98%
- **Evidence**:
  - `core/input/input_map.h` line 54: `static inline InputMap *singleton = nullptr;` — classic singleton pattern
  - Data structure: `HashMap<StringName, Action> input_map` where Action contains `{int id, float deadzone, List<Ref<InputEvent>> inputs}`
  - Registered as engine singleton at `core/register_core_types.cpp:383`: `Engine::get_singleton()->add_singleton(Engine::Singleton("InputMap", InputMap::get_singleton()))`
  - Actions are identified by StringName keys mapping to arrays of InputEvent references
- **Document Accuracy**: CORRECT. The characterization as "a global, static registry that associates string-based action names with arrays of hardware events" is precisely what the code shows.

### H2: No prioritized contexts exist in Godot's native input system
- **Status**: ✅ CONFIRMED
- **Confidence**: 95%
- **Evidence**:
  - Searched `core/input/` for "context", "priority", "stack" — only match is a comment about editor context, not a feature
  - InputMap is a flat HashMap with no layering, priority, or grouping mechanism
  - No API exists to temporarily overlay, mask, or prioritize mappings
  - Runtime remapping requires `erase_action` + `add_action` (destructive mutation)
- **Document Accuracy**: CORRECT. The claim "Mappings cannot be grouped into prioritized contexts" is verified. Remapping at runtime does require deleting/re-adding events.
- **Nuance**: The SceneTree does have an input propagation order (nodes process in reverse tree order, with `is_input_handled()` stopping propagation), but this is node-ordering, not action-context prioritization.

### H3: No extensible processing pipeline (modifiers/triggers) exists natively
- **Status**: ✅ CONFIRMED
- **Confidence**: 95%
- **Evidence**:
  - "Modifiers" in Godot refer to keyboard modifier keys (Shift/Ctrl/Alt/Meta) via `InputEventWithModifiers` class — NOT a processing pipeline
  - "Triggers" in Godot refer to gamepad trigger axes (JoyAxis::TRIGGER_LEFT/RIGHT) — NOT a trigger evaluation system
  - Deadzone is a single float per action (line 51: `float deadzone`), applied during `action_match()` — no chaining, no custom processing
  - `Input::get_vector()` has built-in deadzone logic but is NOT extensible by users at the engine level
  - No equivalent to Unreal's UInputModifier (sequential pre-processors) or UInputTrigger (state machine evaluators)
- **Document Accuracy**: CORRECT. The claim about no formal pipeline for custom pre-processors or triggers is accurate. The deadzone system is baked-in and per-action, not an extensible pipeline.

### H4: Input propagation is main-thread coupled through SceneTree
- **Status**: ✅ CONFIRMED
- **Confidence**: 99%
- **Evidence**:
  - `viewport.cpp:3489` — `push_input()` begins with `ERR_MAIN_THREAD_GUARD` macro
  - `ERR_MAIN_THREAD_GUARD` (node.h:937) = `ERR_FAIL_COND_MSG(is_inside_tree() && !is_current_thread_safe_for_nodes(), ...)` — hard assertion that input processing must be on main thread
  - `input.cpp:803` — `_parse_input_event_impl()` has: `DEV_ASSERT(Thread::get_caller_id() == Thread::get_main_id())` — explicit main-thread assertion
  - Flow: OS event → `parse_input_event()` → buffered → `flush_buffered_events()` → `_parse_input_event_impl()` → `event_dispatch_function` → Viewport → `push_input()` → SceneTree group propagation
  - `scene_tree.cpp:1430` — `_call_input_pause()` uses `_THREAD_SAFE_METHOD_` mutex but propagates sequentially through nodes
- **Document Accuracy**: CORRECT. The claim "native input propagation system runs sequentially through the SceneTree" and is main-thread coupled is precisely accurate.

### H5: GDExtension API supports the proposed CIP architecture
- **Status**: ✅ CONFIRMED (with caveats)
- **Confidence**: 90%
- **Evidence**:
  - `make_virtuals.py` generates GDVIRTUAL macros that support GDExtension virtual calls via `call_virtual_with_data` / `GDExtensionClassCallVirtual`
  - `gdextension.h` has `get_virtual_func` and `get_virtual_call_data_func` for virtual method dispatch
  - Resource inheritance, ClassDB binding, signals, and typed arrays are all available through godot-cpp
  - The proposed classes (CIPAction : Resource, CIPModifier : Resource, CIPTrigger : Resource, CIPSubsystem : Node) are all valid GDExtension patterns
- **Caveats/Issues in the Document's Code**:
  1. `GDVIRTUAL2RC` syntax in the document may not exactly match godot-cpp (godot-cpp uses different macro names than the engine core)
  2. The document uses `GDVIRTUAL_CALL(modifier, _modify, ...)` on an external object — this should be `GDVIRTUAL_CALL_PTR(modifier.ptr(), _modify, ...)` in engine code
  3. Using `Ref<InputEvent>` in atomic/lock-free buffers is problematic — Ref<> is NOT thread-safe by default (reference counting is not atomic in all configurations)

### H6: _unhandled_input is a viable integration point for CIP
- **Status**: ✅ CONFIRMED
- **Confidence**: 95%
- **Evidence**:
  - Verified propagation chain in `viewport.cpp:3535-3548`:
    1. `_input` group called first
    2. `_gui_input_event()` called (UI/Control processing)
    3. `_push_unhandled_input_internal()` called, which processes:
       - Shortcut input
       - Unhandled key input
       - **Unhandled input** ← CIP intercepts here
  - `set_input_as_handled()` correctly stops propagation at each stage via `is_input_handled()` checks
  - A Node implementing `_unhandled_input` in an autoload will receive all non-GUI-consumed events
- **Document Accuracy**: CORRECT. The flow diagram in the document accurately represents the propagation chain. The CIPViewportBridge approach using `_unhandled_input` is architecturally sound.
- **Minor correction**: The document's diagram shows `_gui_input` consuming before CIP, which is correct — but the actual order is `_input` → GUI → shortcut_input → unhandled_key_input → `_unhandled_input`. The document simplifies this slightly.

### H7: WorkerThreadPool can be used for input offloading as described
- **Status**: ⚠️ PARTIALLY CONFIRMED — CRITICAL CAVEAT
- **Confidence**: 60% (feasibility as described)
- **Evidence**:
  - WorkerThreadPool EXISTS at `core/object/worker_thread_pool.h` with confirmed API:
    - `add_native_task(void (*p_func)(void *), void *p_userdata, bool p_high_priority, const String &p_description)` ✅
    - `wait_for_task_completion(TaskID p_task_id)` ✅
    - `add_template_task()` for method pointers ✅
  - **CRITICAL PROBLEM**: The document proposes offloading `process_input_event()` to a worker thread, but this function calls `CIPSubsystem::get_singleton()->process_input_event()`. If this eventually emits signals on CIPAction objects that are in the scene tree, those signal emissions MUST happen on the main thread (ERR_MAIN_THREAD_GUARD)
  - `_parse_input_event_impl` has `DEV_ASSERT(Thread::get_caller_id() == Thread::get_main_id())` — any path that touches Viewport/Input singleton from a worker thread will crash
  - The document's approach of "wait_for_task_completion" defeats the purpose of offloading — it blocks the calling thread until the task finishes
  - Ref<InputEvent> in the ring buffer is NOT thread-safe without additional atomic reference counting
- **Document Accuracy**: PARTIALLY INCORRECT. WorkerThreadPool exists and has the described API, but the proposed threading architecture has fundamental flaws:
  1. Signal emissions from worker threads into scene tree nodes would violate main-thread guards
  2. The `wait_for_task_completion` pattern shown is synchronous blocking, not truly parallel
  3. Ref<> objects in lock-free buffers require careful thread-safety handling not addressed

---

## Competing Hypotheses

### Hypothesis A: The CIP architecture is fully feasible as described
- **Confidence**: 40%
- **Supporting**: Core architecture (contexts, modifiers, triggers, _unhandled_input interception) is sound and well-designed
- **Against**: Threading approach has fundamental flaws; some code details are incorrect for godot-cpp

### Hypothesis B: The CIP architecture is feasible with modifications to threading
- **Confidence**: 85%
- **Supporting**: All non-threading claims are verified correct. The modifier/trigger pipeline, prioritized contexts, and _unhandled_input integration are all architecturally sound. Threading could work if:
  - Modifier/trigger math is computed on worker thread
  - Signal emissions are deferred to main thread via `call_deferred()`
  - Ring buffer uses raw data (not Ref<>) with explicit ownership transfer
- **Against**: Adds complexity; the performance gain may be minimal for typical games

### Hypothesis C: The document is mostly a well-informed design document with AI-generated filler
- **Confidence**: 70%
- **Supporting**: 
  - Core architectural claims about Godot are 100% accurate
  - The Unreal EnhancedInput analysis appears thorough and consistent with known documentation
  - Code examples contain subtle errors typical of LLM generation (GDVIRTUAL_CALL on wrong object, Ref<> in lock-free buffers without safety)
  - Performance complexity claims (O(n*m)) are theoretically correct but not empirically validated
  - References are real URLs but some numbered citations don't clearly support their attributed claims

---

## Final Assessment

### Document Quality Score: 7.5/10

**Strengths:**
1. Accurate characterization of Godot's InputMap limitations (100% verified)
2. Sound architectural design for the CIP module (contexts, modifiers, triggers)
3. Correct understanding of Viewport input propagation chain
4. Valid integration approach via _unhandled_input
5. Good use of Godot-native patterns (Resource, signals, autoloads)

**Weaknesses:**
1. Threading proposal is flawed — doesn't account for ERR_MAIN_THREAD_GUARD on signal emissions and Viewport interactions
2. Code examples have subtle errors (GDVIRTUAL_CALL syntax, Ref<> thread safety)
3. The "lock-free ring buffer" section is architecturally naive — waiting for task completion immediately after submission provides no parallelism benefit
4. Some performance claims are theoretical without empirical backing
5. The document conflates godot-cpp GDExtension macros with engine-internal macros

**Recommendation:**
The core CIP design (prioritized contexts + modifier/trigger pipeline + _unhandled_input interception) is architecturally sound and addresses real gaps in Godot's input system. The threading proposal should be redesigned to:
- Process modifier math on worker threads
- Use `call_deferred()` for signal emissions back to main thread
- Use a proper double-buffer pattern with explicit synchronization points at frame boundaries
- Avoid Ref<> in shared buffers; use raw event data structs instead

---

## Pre-Implementation Questions — RESOLVED

### Item 1: GDVIRTUAL on Resource Subclasses ✅

**No limitations.** GDVIRTUAL works identically on Resource and Node classes — it's an Object-level feature.

**Pattern for CIP:**
```cpp
// cip_modifier.h
class CIPModifier : public Resource {
    GDCLASS(CIPModifier, Resource);
protected:
    static void _bind_methods();
    GDVIRTUAL2RC(Variant, _modify, Variant, double)  // 2 args, Returns Variant, Const
};

// cip_modifier.cpp
void CIPModifier::_bind_methods() {
    GDVIRTUAL_BIND(_modify, "current_value", "delta")
}

Variant CIPModifier::modify(const Variant &p_value, double p_delta) const {
    Variant ret = p_value;
    GDVIRTUAL_CALL(_modify, p_value, p_delta, ret);
    return ret;
}
```

**Evidence:** Texture2D, StyleBox, Resource base class itself all use GDVIRTUAL.
- `scene/resources/texture.h:48-60` — GDVIRTUAL0RC, GDVIRTUAL2RC, GDVIRTUAL4C patterns
- `core/io/resource.h:117-122` — Resource already includes gdvirtual.gen.h

### Item 2: Execution Model ✅

**Decision: Synchronous processing in `_unhandled_input`. No threading for v1.**

**Main loop timing (from `main/main.cpp:4970-5060`):**
1. For each physics step: `flush_buffered_events()` → events dispatched → `physics_process`
2. After physics: `flush_buffered_events()` → events dispatched → `_process`

**Key insight:** Each `flush_buffered_events()` call triggers `_parse_input_event_impl()` → viewport `push_input()` → our `_unhandled_input`. Events arrive one-at-a-time (not batched to the receiver). Processing is synchronous on main thread.

**Performance justification:** Even 100 actions × 10 modifiers × 5 triggers = 5000 evaluations per event. A modifier is a simple math op (deadzone = one `inverse_lerp`). At typical input rates (~60 events/sec for keys, ~120 for analog), this is < 0.1ms per frame total. No threading needed.

**For VR/high-polling future:** Could batch-accumulate analog events in `_process()` rather than evaluating every 8kHz mouse event individually. But this is v2.

### Item 3: InputEvent Matching ✅

**Use `action_match()` — it's exactly what CIP needs.**

```cpp
// How to match in CIP:
// registered_event is the stored mapping (e.g., InputEventKey for 'W')
// incoming_event is the physical event
bool matched = registered_event->action_match(
    incoming_event,
    false,          // exact_match=false → allow superset modifiers
    deadzone,       // from CIPAction or CIPActionMapping
    &pressed,       // out: is the action pressed?
    &strength,      // out: normalized strength 0.0-1.0
    &raw_strength   // out: raw value before deadzone
);
```

**Critical semantics:**
- `this` = registered/stored event template, `p_event` = incoming physical event
- Joypad axis direction: sign of stored `axis_value` determines which direction matches
- Returns true for opposite direction too, but `pressed=false` and `strength=0.0`
- Modifiers: stored modifiers must be subset of incoming (allows extra held keys)
- `is_match()` is simpler (no strength/pressed output) — use only for conflict detection

### Item 4: Resource Serialization ✅

**Use `TypedArray<CIPActionMapping>` with `MAKE_RESOURCE_TYPE_HINT`.**

```cpp
// cip_mapping_context.h
TypedArray<CIPActionMapping> mappings;

// cip_mapping_context.cpp
void CIPMappingContext::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_mappings", "mappings"), &CIPMappingContext::set_mappings);
    ClassDB::bind_method(D_METHOD("get_mappings"), &CIPMappingContext::get_mappings);

    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "mappings", PROPERTY_HINT_ARRAY_TYPE,
            MAKE_RESOURCE_TYPE_HINT("CIPActionMapping")),
        "set_mappings", "get_mappings"
    );
}
```

**Evidence:** `scene/resources/font.cpp:108` (Font fallbacks), `scene/resources/compositor.cpp:213` (compositor_effects). Both use `PROPERTY_HINT_ARRAY_TYPE` + `MAKE_RESOURCE_TYPE_HINT("ClassName")`.

Sub-resources are saved inline in the parent `.tres` file by default — no separate files needed.

---

## Self-Critique Log
- Initial approach was correct: examine source code directly rather than relying on documentation
- Successfully identified the ERR_MAIN_THREAD_GUARD issue which is the document's biggest technical flaw
- All 4 pre-implementation questions resolved entirely from local source — no external research needed
- The document's proposed GDVIRTUAL usage was close but had syntax errors (GDVIRTUAL_CALL on external object needs PTR variant); our pattern is confirmed correct
- Ready to implement Phase 1
