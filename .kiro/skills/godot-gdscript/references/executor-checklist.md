# Executor Checklist

Use this before coding a bounded GDScript task.

## Pre-Implementation Checks

- task has clear targets
- exact target files/scenes/resources have been inspected
- nearby integration points have been inspected
- acceptance criteria are explicit
- needed node types are known
- scene/resource decisions are already approved
- save/load concerns are already delegated if needed
- validation path is known
- if editor-connected, the exact `refresh_filesystem` and LSP validation steps are known

If any item is missing, stop and escalate instead of improvising.

## Default Conventions

- prefer typed variables and return types
- use `_physics_process()` for movement and deterministic mechanics
- use `_process()` for visual-only updates
- choose `preload()`, `load()`, or threaded loading based on actual usage and hitch tolerance
- use Godot 4 signal syntax and lifecycle callbacks

## Validation Ladder

Prefer, in this order:

- editor-connected validation
  - `refresh_filesystem` after disk edits the editor must see
  - `get_diagnostics` on changed scripts
  - `search_symbols`, `get_definition`, or `get_hover_info` when symbol grounding is still unclear
  - `find_nodes` for targeted scene-tree lookups by name, type, or group when verifying node setup without reading the full tree
  - `run_scene` after diagnostics pass to catch runtime errors that compile-clean code still produces (e.g., .tres parse failures, `_ready()` crashes, missing resources, wrong node paths). Compile-clean does not mean plays correctly — `run_scene` bridges this gap. Use it on the main scene or on specific scenes that exercise the new code.
  - `editor_screenshot` after `run_scene` to visually verify the running game when visual correctness matters (UI layout, shader effects, spatial placement). Requires the game to be running.
- headless or engine-backed static validation
- source-only reasoning

State explicitly which level was reached.

## Common Footguns

- old Godot 3 APIs or signal syntax
- using `_process()` for mechanics that should live in physics
- accessing nodes before they are ready
- quietly changing architecture from inside the task
- introducing non-GDScript runtime choices without approval
- trusting stale editor diagnostics without `refresh_filesystem`
- `@export var x: Node2D` (or any Node-derived type) on a Resource subclass — this is illegal. Resource exports can only reference primitive types, other Resources, or NodePath. Use `@export var target_path: NodePath` and resolve at runtime via `owner.get_node_or_null(target_path)`.
- writing subclasses before the base class compiles. When implementing a class hierarchy, always validate the base class with `get_diagnostics` (or equivalent) before writing any subclass. A single parse error in the base cascades to every subclass as "Could not resolve class" — producing N misleading errors from one root cause.
- **Typed `Array[CustomResource]` in hand-written .tres files.** When writing `.tres` files by hand that contain arrays of custom Resources, always declare the array as `Array[Resource]` in the script, never `Array[YourCustomType]`. Godot's `.tres` serialization stores script references as paths, not `class_name` aliases. At editor time this causes type mismatch diagnostics; at runtime it causes `Invalid cast` errors. Use `.get()` to access properties at runtime. This is the #1 recurring bug in data-driven games.
- **`change_scene_to_file()` / `add_child()` on tree root in `_ready()`.** Never call scene-changing methods or tree-root mutations inside `_ready()`. The tree is still adding children, so `remove_child()` triggers "Parent node is busy adding/removing children." Always use `call_deferred()` for scene transitions and root-level tree mutations from `_ready()`.
- **Integer division warnings (Godot 4.6).** Godot 4.6 warns on every integer division. For tile math, grid math, and damage calculations this is constant noise. Silence with explicit cast: `int(x / float(y))` or `floori(x / float(y))`. Not a bug, just noisy.
- **Shadowing built-in math functions.** Avoid using these as variable names: `exp`, `log`, `sin`, `cos`, `abs`, `min`, `max`, `round`, `sign`, `clamp`, `lerp`, `sqrt`. Godot warns when a local variable shadows a built-in function. Use descriptive names instead: `experience`, `log_entry`, `min_value`, etc.
- **`null as TypedClass` in dictionary/array literals.** `null as MeshInstance3D` (or any typed class) in a Dictionary or Array literal is an invalid cast in GDScript 4.6. The parser rejects casting `null` to a concrete type. Use plain `null` — dictionaries and arrays are Variant-valued and don't benefit from null casts. Cast when reading instead: `(dict.mesh as MeshInstance3D).queue_free()`.
- **`queue_redraw()` every frame at scale.** Node2D transform properties (`position`, `rotation`, `scale`) are updated by the engine without triggering `_draw()`. Calling `queue_redraw()` forces a full `_draw()` dispatch per node per frame — at 3000 nodes this is 50x worse than using `rotation=`. Reserve `_draw()` + `queue_redraw()` for static or rarely-changing visuals (archetype change, type swap). For per-frame visual updates at scale, use `canvas_item_set_transform()`.
- **Silent error recovery.** Do not write code that catches errors and silently continues with a default value. See the Error Handling section below.

## Error Handling Defaults

Follow the foundation error handling philosophy (Operating Model section 7). In practice this means:

**Default to fail-fast.** When something is wrong, make it loud and visible. Do not write recovery code unless the architect or task explicitly classifies the error as Category B or C.

**Concrete patterns:**

```gdscript
# Category A: Programmer error. Crash or error + return.
func load_wave(wave_index: int) -> void:
    assert(wave_index >= 0 and wave_index < _wave_data.size(),
        "load_wave: index %d out of range [0, %d)" % [wave_index, _wave_data.size()])
    # ...

# Category A: Missing required reference. Error + return.
func _ready() -> void:
    _player = get_node_or_null("Player") as CharacterBody2D
    if _player == null:
        push_error("GameManager: required Player node not found")
        return
    # ...

# Category B: Missing asset. Visible degradation, not silent.
# Only use when architect explicitly classifies as Category B.
func _load_texture(path: String) -> Texture2D:
    if not ResourceLoader.exists(path):
        push_error("_load_texture: missing asset at %s" % path)
        return null  # Caller shows placeholder or skips rendering
    return load(path) as Texture2D

# WRONG: Silent recovery. Never do this.
func _load_texture_bad(path: String) -> Texture2D:
    if not ResourceLoader.exists(path):
        return _default_texture  # Bug is now invisible
```

**When the task includes error_notes from the orchestrator**, follow the classification. If a task says "Error: missing scene path is Category A per architect", use `push_error()` + return, not a fallback.

**When no error classification is provided**, treat all errors as Category A (crash/assert). The architect is responsible for classifying errors that need softer handling. If the architect did not classify it, it is a bug.

## Task Output Pattern

For each coding task, be able to name:

- scripts or nodes added/changed
- integration points inspected
- MCP sync or LSP steps used
- callbacks used
- groups/signals introduced
- validation method
- exact blocker if work cannot continue honestly

## Testability Defaults

These apply to all new code. See `GODOT-CODING-RULES.md` Rule 12 (Writing Testable Code) for the full rationale.

- separate data logic (calculations, state transitions) from scene wiring (node callbacks, input handling)
- use typed returns and typed properties so tests can use type-specific assertions
- expose key state through readable properties or signals, not only through side effects
- add `push_warning()` at unexpected-but-recoverable decision points for traceability
- prefer `@export` injection over hard-coded `preload()` for dependencies that tests need to substitute
- keep methods focused and single-purpose so unit tests can target them individually

## Performance Profiling Protocol

- **Use `Time.get_ticks_usec()` as ground truth.** The script profiler shows inclusive time that includes engine-internal overhead (Node tree traversal, notification dispatch). When profiler time >> manual timing for a function, the gap is per-Node engine overhead — reduce Node count, not script complexity.
- **Read ALL top-level profiler entries before optimizing.** Engine subsystems (Physics 2D, Physics 3D, Rendering, Audio) appear as separate entries. A 49ms Physics 2D entry is a bigger win than shaving 10ms off your GDScript loop. Always fix the largest wall first.
- **Count bridge crossings for hot loops.** Each GDScript→Engine API call costs ~0.25μs. At N entities: `entities × calls_per_entity`. If that exceeds ~10,000 crossings/frame, the loop structure is the bottleneck regardless of what each call does.

## Escalate

Escalate to:

- `godot-architect` when the task needs a new pattern
- `godot-scene-resource` when mutable resources or instancing become risky
- `godot-persistence` when runtime state must survive sessions
- `godot-gdunit4` when the task needs test coverage or test validation
