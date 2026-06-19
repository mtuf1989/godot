# CommonUI Framework API Reference

## Table of Contents

1. [Core Types](#core-types)
2. [UIRouter API](#uirouter-api)
3. [UIInputRouter API](#uiinputrouter-api)
4. [TransitionManager API](#transitionmanager-api)
5. [SceneRouter API](#scenerouter-api)
6. [UIThemeManager API](#uithememanager-api)
7. [InputGlyphDB API](#inputglyphdb-api)
8. [InputGlyph API](#inputglyph-api)
9. [Context Dictionary Conventions](#context-dictionary-conventions)

---

## Core Types

All core types live in `scripts/` and use `class_name` declarations.

### UIActivatable (`scripts/ui_activatable.gd`)

Base class for all navigable UI scenes. Extends `Control`.

5 required methods (must be overridden):

```gdscript
# Called when scene becomes the active top of its layer stack.
# Receives data via context dictionary.
func activate(context: Dictionary) -> void

# Called when scene is deactivated. reason tells you why.
func deactivate(reason: DeactivateReason.Reason) -> void

# Returns the Control node that should receive focus for gamepad/keyboard navigation.
# Return null if the scene should not steal focus (e.g. HUD overlays).
func get_desired_focus_target() -> Control

# Returns the input configuration for this scene.
func get_desired_input_config() -> UIInputConfig

# Called when ui_cancel is pressed. Return true if you handled it.
# Return false to let the system pop this scene automatically.
func on_back_requested() -> bool
```

Optional overrides:

```gdscript
# Called instead of activate() when a covering screen is popped and this screen is uncovered.
# Default implementation delegates to activate(). Override when first-activation logic
# differs from uncovering logic (e.g. expensive _build_ui() vs cheap _refresh()).
func reactivate(context: Dictionary) -> void
```

Built-in utilities:
- `require_context_keys(context: Dictionary, required_keys: Array[String]) -> bool` -- Returns false and logs `push_error` per missing key with script path. Use at top of `activate()` to catch misconfigured navigation early.
- `_validate_interface() -> bool` -- Runtime check that all 5 methods are implemented.
- `get_validation_report() -> Dictionary` -- Detailed validation report.

### UILayer (`scripts/ui_layer.gd`)

Enum for the 5 UI layers. Extends `RefCounted`.

```gdscript
enum Layer {
    GAME_HUD,    # Priority 0  -- In-game overlays
    GAME_MENU,   # Priority 10 -- In-game menus (pause, inventory)
    MENU,        # Priority 20 -- Out-of-game UI (main menu, settings)
    MODAL,       # Priority 30 -- Blocking dialogs
    LOADING      # Priority 40 -- Loading states
}
```

Static method: `UILayer.get_layer_priority(layer: Layer) -> int`

Higher priority layers block input to lower layers automatically.

### UIInputConfig (`scripts/ui_input_config.gd`)

Resource for per-scene input configuration. Extends `Resource`.

```gdscript
enum InputMode {
    GAME,    # Allow gameplay input, maintain cursor state
    MENU,    # Block gameplay input, show cursor
    ALL,     # Allow both UI and gameplay input
    LOCKED   # Block all input except system actions
}

# Constructor
UIInputConfig.new(mode: InputMode, show_cursor: bool, capture_cursor: bool)
```

### DeactivateReason (`scripts/deactivate_reason.gd`)

Enum for deactivation context. Extends `RefCounted`.

```gdscript
enum Reason {
    COVERED,   # Another scene pushed on top
    POPPED,    # This scene was popped from stack
    REPLACED,  # This scene was replaced by another
    CLEARED    # The entire layer was cleared
}
```

### DeviceType (`scripts/device_type.gd`)

Enum for input device family. Extends `RefCounted`.

```gdscript
enum Type {
    KEYBOARD_MOUSE,
    XBOX,
    PLAYSTATION,
    SWITCH,
    GENERIC_GAMEPAD
}
```

---

## UIRouter API

Autoload at `/root/UIRouter`. Central navigation orchestrator.

### Properties

```gdscript
# Maximum stack depth per layer (default 32). Push is rejected if exceeded. Set 0 for unlimited.
var max_stack_depth: int = 32

# Log verbosity: 0 = errors only, 1 = errors + warnings, 2 = everything (default)
var log_level: int = 2
```

### Navigation Operations

```gdscript
# Push a scene onto a layer stack
push_to_layer(layer: UILayer.Layer, scene_path: String, context: Dictionary = {}) -> void

# Pop the top scene from a layer stack (destroys it)
pop_from_layer(layer: UILayer.Layer) -> void

# Pop whatever screen is currently active, automatically finding its layer
pop_active() -> void

# Atomically replace the top scene in a layer
replace_in_layer(layer: UILayer.Layer, scene_path: String, context: Dictionary = {}) -> void

# Clear all scenes from a layer
clear_layer(layer: UILayer.Layer) -> void
```

### Navigation with Transitions

All navigation operations have `_with_transition` variants:

```gdscript
push_to_layer_with_transition(
    layer: UILayer.Layer,
    scene_path: String,
    context: Dictionary = {},
    use_transition: bool = true,
    fade_duration: float = 0.3
) -> void

pop_from_layer_with_transition(
    layer: UILayer.Layer,
    use_transition: bool = true,
    fade_duration: float = 0.3
) -> void

pop_active_with_transition(
    use_transition: bool = true,
    fade_duration: float = 0.3
) -> void

replace_in_layer_with_transition(
    layer: UILayer.Layer,
    scene_path: String,
    context: Dictionary = {},
    use_transition: bool = true,
    fade_duration: float = 0.3
) -> void

clear_layer_with_transition(
    layer: UILayer.Layer,
    use_transition: bool = true,
    fade_duration: float = 0.3
) -> void
```

### Layer Queries

```gdscript
# Find which layer a UIActivatable is on. Returns -1 if not found.
# Searches all layer stacks (not just tops), so works for any screen in any position.
get_layer_for_activatable(target_ui: UIActivatable) -> int
```

### State Inspection and Debugging

```gdscript
# Get the currently active UIActivatable (top of highest-priority non-empty layer)
get_active_ui() -> UIActivatable

# Full debug state dictionary (all layer stacks, active UI, focus state)
get_debug_state() -> Dictionary

# Print debug state to console in readable format
print_debug_state() -> void

# Comprehensive health check (overall_status, issues[], warnings[], layer_health, interface_validation)
perform_health_check() -> Dictionary

# Validate all active UIActivatable interfaces
validate_all_interfaces() -> Dictionary

# Execute debug commands programmatically
# Commands: "state", "health", "validate", "print", "focus_info", "cleanup",
#           "clear_layer" (args: [layer_name]), "push" (args: [layer, path, context]),
#           "pop" (args: [layer]), "help"
execute_debug_command(command: String, args: Array = []) -> Dictionary
```

### Navigation Lifecycle Sequence

When `push_to_layer` is called:
1. Validate layer, scene_path, and context parameters.
2. Instantiate the scene and validate UIActivatable interface.
3. Deactivate current top with `deactivate(COVERED)`.
4. Store focus state of current top.
5. Push new instance onto the layer stack.
6. Call `activate(context)` on the new instance.
7. Call `get_desired_focus_target()` and apply focus.
8. Call `get_desired_input_config()` and apply via UIInputRouter.

When `pop_from_layer` is called:
1. Deactivate current top with `deactivate(POPPED)`.
2. Remove from stack and `queue_free()`.
3. Reactivate previous top with `reactivate(context)` (which delegates to `activate()` unless overridden).
4. Restore focus state of previous top.
5. Apply previous top's input config.

---

## UIInputRouter API

Autoload at `/root/UIInputRouter`. Centralized input authority.

### Properties

```gdscript
# Log verbosity: 0 = errors only, 1 = errors + warnings, 2 = everything (default)
var log_level: int = 2
```

### Methods

```gdscript
# Apply input configuration globally
apply_input_config(config: UIInputConfig) -> void

# Get current input configuration
get_current_input_config() -> UIInputConfig

# Reset to default (MENU mode)
reset_to_default() -> void

# Get the InputDeviceTracker instance
get_device_tracker() -> InputDeviceTracker

# Get debug state (input_mode, show_cursor, mouse_mode, active_ui_script, etc.)
get_debug_state() -> Dictionary

# Run health check
perform_health_check() -> Dictionary
```

### InputDeviceTracker (inner class of UIInputRouter)

```gdscript
# Signal emitted when device family changes
signal device_type_changed(new_type: DeviceType.Type)

# Process an InputEvent and update device type if needed
process_event(event: InputEvent) -> void

# Get the current device type
get_device_type() -> DeviceType.Type

# Force a specific device type (testing/override)
set_device_type(type: DeviceType.Type) -> void
```

Handles `ui_cancel` routing: when pressed, calls `on_back_requested()` on the active UIActivatable. If it returns false, UIInputRouter pops the scene automatically.

---

## TransitionManager API

Autoload at `/root/TransitionManager`. Visual transition effects.

### Properties

```gdscript
# Maximum queued operations (default 8). Excess operations are dropped with error.
var max_queue_size: int = 8

# Log verbosity: 0 = errors only, 1 = errors + warnings, 2 = everything (default)
var log_level: int = 2
```

### Simple Fade Transitions

```gdscript
# Full fade transition with callback
fade_transition(
    fade_out_duration: float = 0.3,
    fade_in_duration: float = 0.3,
    callback: Callable = Callable(),
    block_input: bool = true
) -> void

# Convenience: navigation transition
transition_navigation(navigation_callback: Callable, fade_duration: float = 0.3) -> void

# Convenience: loading screen transition
transition_loading(show_loading: bool, fade_duration: float = 0.3) -> void

# Immediate transition (no fade)
immediate_transition(callback: Callable) -> void
```

### Shader-Based Transitions (Masks, Wipes, Dissolves)

```gdscript
# Cover the screen with a shader-driven transition.
# Options: duration, mask_texture (grayscale), edge_softness, fade_color,
#          show_progress_bar (bool)
transition_in(options: Dictionary) -> void

# Reveal the screen after covering.
# Options: duration
transition_out(options: Dictionary) -> void

# Update progress bar value (0.0 to 1.0) during a transition_in with show_progress_bar.
update_progress(value: float) -> void

# Cancel active transition, clear queue, reset overlay.
cancel_transition() -> void

# Check transition state
is_transitioning() -> bool

# Configure fade appearance
set_fade_color(color: Color) -> void
get_fade_color() -> Color

# Debug state (is_transitioning, queue_size, fade_overlay_alpha, shader_available, etc.)
get_debug_state() -> Dictionary
```

### Signals

```gdscript
signal transition_completed()   # Emitted when any transition finishes
signal screen_covered()         # Emitted when transition_in() fully covers the screen
signal progress_bar_filled()    # Emitted when progress bar reaches 1.0
```

### Mask Texture Patterns

The shader uses a grayscale mask to control the reveal pattern:
- Solid white = uniform fade (default, no mask needed)
- Gradient texture = directional wipe
- Noise texture = dissolve effect
- Diamond/circle pattern = shaped wipe

### Operation Queuing

If `fade_transition()` is called while a transition is running, the operation is queued (up to `max_queue_size`). Operations execute in order after the current transition finishes. This prevents race conditions from rapid navigation.

Uses a full-screen ColorRect on CanvasLayer priority 100. Blocks input via LOCKED mode during transitions when `block_input` is true.

---

## SceneRouter API

Autoload at `/root/SceneRouter`. World/level scene transitions (separate from UI navigation).

### Cleanup Policy Constants

```gdscript
const CLEANUP_TRANSIENT: int = 0  # Clears GAME_HUD, GAME_MENU, MODAL (default)
const CLEANUP_ALL: int = 1        # Clears GAME_HUD, GAME_MENU, MENU, MODAL
const CLEANUP_NONE: int = 2       # Clears nothing
const CLEANUP_CUSTOM: int = 3     # Clears only layers listed in options["cleanup_layers"]
# LOADING is never cleared by any policy. SceneRouter manages it explicitly.
```

### World Transitions

```gdscript
# Change to a new world scene. Returns bool: false if rejected or failed, true on success.
# Triggers an 8-phase sequence: cover → loading screen → async load → cleanup → min duration → swap → clear loading → reveal.
# Layer cleanup happens after loading succeeds, so a failed load preserves existing UI state.
change_world(scene_path: String, context: Dictionary = {}, options: Dictionary = {}) -> bool

# Go back to previous world (if history exists). Uses skip_history internally.
go_back() -> void

# Check if back navigation is possible (history has 2+ entries)
can_go_back() -> bool

# Restart the current world with same context
restart_world() -> void

# Restart the current world with new context
restart_world_with_context(context: Dictionary) -> void

# Check if a transition is in progress
is_transitioning() -> bool

# Get debug state (current_world_path, is_transitioning, history_size, cache_size, etc.)
get_debug_state() -> Dictionary
```

### Scene Caching

Bounded LRU cache (8 entries by default). Cache hits skip the loading phase entirely.

```gdscript
# Preload a scene for instant future transitions
preload_scene(scene_path: String) -> void

# Remove a specific scene from cache
evict_cached_scene(scene_path: String) -> void

# Clear entire cache
clear_scene_cache() -> void

# Get current cache size
get_cache_size() -> int
```

### History

LIFO history of visited worlds (max 32 entries).

```gdscript
can_go_back() -> bool
go_back() -> void
get_history_size() -> int
```

### Signals

```gdscript
signal world_transition_started(scene_path: String)
signal world_transition_finished(scene_path: String)
signal world_load_progress(progress: float)
```

### Options Dictionary

```gdscript
{
    # Fade timing
    "fade_out_duration": float,       # default 0.3
    "fade_in_duration": float,        # default 0.3
    "min_duration_ms": float,         # minimum transition time (default 300)

    # Loading screen
    "show_loading": bool,
    "loading_scene": String,          # custom loading screen path
    "loading_context": Dictionary,    # context for loading screen

    # Layer cleanup
    "cleanup_policy": int,            # CLEANUP_TRANSIENT/ALL/NONE/CUSTOM
    "cleanup_layers": Array,          # layer list for CLEANUP_CUSTOM

    # Shader transitions
    "use_shader_transition": bool,
    "mask_texture": Texture2D,        # grayscale mask for wipe/dissolve
    "edge_softness": float,
    "fade_color": Color,
    "show_progress_bar": bool,

    # History
    "skip_history": bool,             # don't record in history

    # Caching
    "use_cache": bool,                # check cache before async load (default true)

    # Typed context
    "transition_context": TransitionContext,  # resource for apply_transition_context()
}
```

### Context Injection into Worlds (Duck Typing)

SceneRouter injects context into the new world scene in this order:
1. If world has `apply_transition_context(ctx: TransitionContext)` and a TransitionContext was provided: called first
2. If world has `apply_context(context: Dictionary)`: called with the context dict
3. Else if world has `activate(context: Dictionary)`: called as fallback

World scenes implement one of these methods (not UIActivatable).

---

## UIThemeManager API

Autoload at `/root/UIThemeManager`. Theme preset management.

```gdscript
# Apply a named theme preset project-wide
apply_theme(preset_name: String) -> void

# Get the name of the currently active theme preset
get_current_theme() -> String

# Get all registered preset names
get_available_themes() -> Array[String]

# Register a custom theme preset at runtime
register_theme(preset_name: String, theme: Theme) -> void

# Remove a registered theme preset
unregister_theme(preset_name: String) -> void

# Signal
signal theme_changed(preset_name: String)
```

Auto-discovers presets from `res://themes/theme_*.tres` on startup. Applies Theme to UIRouter CanvasLayer children so it cascades to all pushed scenes. Persists user preference to `user://settings.cfg`.

Built-in type variations: PrimaryButton, SecondaryButton, DangerButton, HeaderLabel, BodyLabel, CaptionLabel, PanelCard, PanelOverlay.

---

## InputGlyphDB API

Resource at `res://ui/glyphs/glyph_db.tres`. Resolves action names to device-specific textures.

```gdscript
# Resolve action to texture. Fallback: override -> device -> GENERIC_GAMEPAD -> KEYBOARD_MOUSE
resolve(action_name: StringName, device_type: DeviceType.Type) -> Texture2D

# Lower-level per-event lookup
resolve_event(event: InputEvent, device_type: DeviceType.Type) -> Texture2D

# Set/clear custom overrides
set_override(action_name: StringName, device_type: DeviceType.Type, texture: Texture2D) -> void
clear_override(action_name: StringName, device_type: DeviceType.Type) -> void

# Rescan glyph directories
reload() -> void
```

Directory convention: `res://ui/glyphs/{keyboard_mouse,xbox,playstation,switch,generic_gamepad}/`

---

## InputGlyph API

Drop-in TextureRect node. Displays the correct glyph for an input action.

```gdscript
# Exported properties
@export var action_name: StringName
@export var glyph_db: InputGlyphDB
@export var animate_swap: bool = true
@export var swap_duration: float = 0.15
@export var show_hold_progress: bool = false

# Change action and re-resolve
set_action(new_action: StringName) -> void

# Force re-resolve
refresh() -> void
```

Auto-connects to `InputDeviceTracker.device_type_changed` on `_ready()`. Swaps texture when device changes.

---

## Context Dictionary Conventions

Supported value types: `bool`, `int`, `float`, `String`, `Vector2`, `Vector3`, `Array`, `Dictionary`, `Resource`, `null`.

Unsupported types generate warnings and are excluded.

Common context patterns:

```gdscript
# Main menu
{"player_name": String, "version": String, "has_save_data": bool}

# Game HUD
{"player_name": String, "level": int, "health": int, "max_health": int, "score": int}

# Settings
{"return_to": String, "current_settings": Dictionary}

# Modal dialog
{"title": String, "message": String, "confirm_action": String, "cancel_action": String}

# Loading screen
{"title": String, "message": String, "auto_load": bool, "next_scene": String, "next_layer": String, "next_context": Dictionary}
```
