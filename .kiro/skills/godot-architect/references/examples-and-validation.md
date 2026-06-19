# Examples and Validation

Use this file before finalizing the architecture decision summary.

## Example 1: Simple Feature (no extended sections needed)

Scoped slice:

- one arena survival loop
- player node, enemy nodes, wave progression
- possible future save/load
- enemy tuning data reused across instances

### Good Output Shape

```md
# Architecture Decision

## Problem
- Build a reusable arena survival slice with enemy instances and future persistence headroom.

## Chosen Approach
- Use composition-based scenes.
- Keep mutable runtime state on nodes/components.
- Store shared enemy tuning in immutable or rarely mutated resources.
- Default implementation runtime is GDScript.

## Compatibility Constraints
- preserve current top-down 2D scene structure
- preserve existing input map rather than replacing controls

## Rejected Alternatives
- scene inheritance-heavy enemy hierarchy -> brittle under refactor
- mutable shared Resource for per-enemy state -> hidden state leakage risk
- early C# or GDExtension -> no proven need

## Risks
- mutable resource leakage across enemy instances -> consult godot-scene-resource
- future persistence coupling -> consult godot-persistence before implementation

## Follow-On Skills
- godot-scene-resource
- godot-orchestrator
```

## Example 2: Complex Feature (extended sections included)

Scoped slice:

- scene transition system with LRU cache, shader-based transitions, progress bar
- must be backward compatible with existing fade-based transitions
- multiple components that must agree on an API (TransitionManager, SceneRouter, SceneCache)

### Good Output Shape

```md
# Architecture Decision

## Problem
- Add shader-based transitions and scene caching to the existing transition system without breaking the current fade-based API.

## Chosen Approach
- Upgrade TransitionManager with ShaderMaterial overlay driven by a progress uniform and mask texture.
- Add SceneCache as an inner class of SceneRouter (bounded LRU, strong references with eviction).
- New capabilities are opt-in via option keys on the existing change_world() API.
- Solid-white mask produces identical fade to current behavior (backward compatible by default).

## Compatibility Constraints
- all existing change_world() calls work without modification
- use_shader_transition defaults to false
- existing fade_transition() API preserved

## Rejected Alternatives
- TextureRect for overlay -> conflates node visual with shader parameter, collapses to zero without texture
- Separate TransitionShader autoload -> unnecessary new autoload, TransitionManager already owns the overlay
- CACHE_MODE_IGNORE for ResourceLoader -> known infinite-loop risk in Godot 4

## Component Interfaces

### TransitionManager (autoload/transition_manager.gd)
Manages visual transitions via shader-driven overlay with progress bar.

Signals:
- signal screen_covered()
- signal progress_bar_filled()

Public API:
- func transition_in(options: Dictionary = {}) -> void
- func transition_out(options: Dictionary = {}) -> void
- func update_progress(progress_ratio: float) -> void
- func set_mask_texture(texture: Texture2D) -> void
- func is_displayed() -> bool

### SceneCache (inner class of SceneRouter)
Bounded LRU cache for loaded PackedScene resources.

Public API:
- func get_scene(path: String) -> PackedScene
- func put_scene(path: String, scene: PackedScene) -> void
- func has_scene(path: String) -> bool
- func evict(path: String) -> void

## Key Algorithm Pseudocode

### LRU Cache Eviction
- get_scene: if cached, move to end of access_order, return scene. If not cached, return null.
- put_scene: if already cached, update and move to end. If full, evict front of access_order. Insert at end.
- Invariant: _access_order.size() == _cache.size() at all times.

### Smooth Progress Interpolation
- _process(delta): drive _current_progress toward _target_progress via move_toward()
- Use _progress_bar_signal_emitted flag to prevent multiple emissions from _process() re-entrance
- Use one-shot timer (not await) to emit progress_bar_filled

## Error Classification

| Scenario | Category | Response |
|----------|----------|----------|
| Shader file missing | B (visible degradation) | push_warning, fall back to modulate-alpha. Shader is optional enhancement. |
| Scene path invalid | A (crash) | push_error + return. Caller passed a bad path. |
| Concurrent transition attempt | A (crash) | push_warning + reject. Code should not call change_world during a transition. |
| Cache eviction during transition | N/A | Not possible. Scene reference is held locally before cache is touched. |

## Correctness Properties

### Property 1: Cache bounded size
For any sequence of put_scene() calls, _cache.size() never exceeds _max_size.

### Property 2: Backward compatibility
For any change_world() call without Phase 5 option keys, behavior is identical to the current implementation.

### Property 3: Progress bar filled fires at most once
For any transition, progress_bar_filled is emitted at most once per transition cycle.

## Risks
- await in _process() re-entrance -> use flag guard + one-shot timer
- progress_bar_filled never fires -> add 5s timeout in transition_out
- double load_threaded_request -> handled gracefully (reuses token), but check status to avoid redundant calls

## Follow-On Skills
- godot-orchestrator
```

## Validation Checklist

- decisions are tied to Godot-specific behavior
- current-project compatibility is explicit
- rejected alternatives are named
- output does not become a full implementation (pseudocode for key algorithms is fine; line-by-line code is not)
- risks and follow-on skills are explicit
- for complex features: component interfaces define contracts, not internals
- for complex features: error scenarios are classified as A/B/C per foundation rules
- for complex features: correctness properties are testable "for any X, Y holds" statements
- extended sections are only present when the feature warrants them

## Bad Signs

- generic advice that could apply to any engine
- no sign that the current project was inspected
- no rationale for runtime choice
- no mention of mutable resource risk when applicable
- 800+ line design document (keep it under 300 even for complex features)
- error scenarios all classified as Category C with recovery paths (default should be Category A)
- correctness properties that are vague aspirations instead of testable assertions

## Escalate

Consult:

- `godot-scene-resource` for instancing/resource semantics
- `godot-persistence` for durable state or reconstruction
