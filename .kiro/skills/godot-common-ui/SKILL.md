---
name: godot-common-ui
description: |
  DEFAULT UI WORKFLOW — Use this skill for ANY UI, popup, screen, menu, HUD, dialog, loading screen,
  scene transition, level transition, or screen-flow request in this project. This is the project's
  UI Manager, Popup Manager, and Scene/Level Manager. It controls all UI navigation and game flow.

  ALWAYS trigger this skill when the user asks to:
  - Create, modify, or wire ANY screen or menu (main menu, pause menu, settings, credits, game over, victory, title screen, splash)
  - Add ANY popup, modal, confirmation dialog, alert, toast, or notification
  - Add ANY HUD, overlay, health bar, score display, minimap, or in-game UI
  - Add ANY loading screen, progress bar, or transition effect
  - Transition between levels, worlds, stages, maps, or game states
  - Set up game flow, screen flow, navigation between screens, or back-button behavior
  - Show or hide UI elements as part of game state changes
  - Wire up "go to next level," "restart level," "return to menu," or any scene/level navigation
  - Add dissolves, fades, wipes, or any screen transition effect
  - Preload or cache scenes/levels for faster transitions

  This skill fires on generic phrases like: "add a menu," "show a popup," "go to next level,"
  "add a loading screen," "wire up pause," "create settings screen," "add game over screen,"
  "transition to level 2," "show confirmation dialog," "set up the UI flow," "add a HUD,"
  "create a splash screen," "add a death screen," "show inventory," "open shop," etc.

  Only defer to godot-ui-core for pure Control layout, anchors-vs-containers, or theme styling
  tasks that have ZERO navigation or screen-management component.
---

# Godot Common UI

Build UIActivatable scenes and wire navigation flows using the CommonUI Navigation Framework's stack-based layer system.

Read `references/framework-api.md` first. Read `references/scene-templates.md` before creating any new UIActivatable scene. Read `references/integration-checklist.md` before finalizing.

## Responsibility

- Inspect the project to confirm CommonUI autoloads are registered and the framework scripts exist before doing anything.
- Create new UIActivatable scenes that implement the 5-method contract correctly, plus optional `reactivate()` when first-activation and uncovering logic differ.
- **Build the UI layout inside each scene** using Control-tree-safe rules from `godot-ui-core`:
  - Anchors for viewport or fixed-bound placement; containers for dynamic content.
  - Proper `custom_minimum_size`, size flags, `mouse_filter`, `focus_mode`, and `process_mode`.
  - Theme resources and `theme_type_variation` for styling.
  - Focus chain setup for keyboard/gamepad navigation.
  - Resolution-safe layout that survives aspect ratio changes.
  - Read `../godot-ui-core/references/ui-decision-table.md` for layout strategy decisions.
  - Read `../godot-ui-core/references/layout-checklist.md` when validating layout correctness.
- Use `require_context_keys()` in `activate()` to validate required context data early.
- Wire navigation flows between scenes using UIRouter's layer stack operations, including `pop_active()` for layer-agnostic back navigation and `get_layer_for_activatable()` for reusable screens.
- Pass context dictionaries between scenes following framework conventions.
- Configure input modes, focus targets, and back navigation per scene type.
- Use TransitionManager for fade effects and shader-based transitions (mask textures, wipes, dissolves, progress bars) between navigation states.
- Use SceneRouter for world/level scene transitions with caching, history, restart, and custom cleanup policies.
- Set up InputGlyph nodes and InputGlyphDB resources for device-aware button prompts.
- Apply themes via UIThemeManager when the project uses theme presets.
- Use the debugging system (`get_debug_state()`, `perform_health_check()`, `execute_debug_command()`) to diagnose navigation issues.
- Validate that all UIActivatable implementations satisfy the interface contract.

## Use When

- ANY UI task in this project: creating screens, menus, HUDs, popups, dialogs, loading screens, or overlays.
- Building the visual layout of a UI scene (health bars, button stacks, settings panels, inventory grids).
- The task needs navigation wiring between screens (push, pop, replace, transitions).
- The task involves context passing, back navigation, or transition effects.
- The task needs level/world transitions, scene caching, or history navigation.
- The task needs InputGlyph setup for controller/keyboard prompt icons.
- The task needs UIThemeManager integration for theme switching.
- The task needs debugging or diagnosing navigation/input/transition issues.
- The task needs anchors-vs-containers decisions, focus chains, or resolution-safe layout — this skill handles it inline using godot-ui-core knowledge.

## Do Not Use When

- The project does not have CommonUI autoloads and the task is purely about Control layout theory with no screen management. Use godot-ui-core directly.
- The task is about scene/resource ownership or duplication safety. Use godot-scene-resource.
- The task is about save/load or persistence. Use godot-persistence.
- Architecture decisions are still unresolved. Use godot-architect.
- The scope is broad game design or feature planning. Use godot-scope.

## Workflow

1. Read `references/framework-api.md`, then inspect the project:
   - Check `project.godot` for autoload registrations: UIRouter, UIInputRouter, TransitionManager, SceneRouter, UIThemeManager.
   - Locate the framework directories: `autoload/`, `scripts/`, and any existing UIActivatable scenes. Use `find_nodes` to locate UIRouter layers or existing UIActivatable nodes in the scene tree.
   - Check for `ui_cancel` input action in the project's InputMap.
   - Note existing theme presets in `themes/` and glyph assets in `ui/glyphs/`.
   - If any required autoload is missing, report the blocker and stop.

2. Determine what the task needs:
   - New UIActivatable scene(s): which layer, what context, what navigation.
   - Navigation wiring: push, pop, replace, clear, with or without transitions.
   - Context dictionary design: what data flows between scenes.
   - Back navigation behavior: custom handling vs default pop.
   - World transitions: SceneRouter.change_world with context and options.

3. Read `references/scene-templates.md`, then create UIActivatable scenes following these rules:
   - Root node is `Control` with `anchors_preset = 15` (FULL_RECT).
   - Script extends `UIActivatable`.
   - All 5 contract methods are implemented with correct signatures and return types.
   - Override `reactivate(context)` when first-activation logic differs from uncovering logic (e.g., expensive `_build_ui()` in `activate()` vs cheap `_refresh()` in `reactivate()`). The default `reactivate()` delegates to `activate()`, so existing code works unchanged.
   - Use `require_context_keys(context, ["key1", "key2"])` at the top of `activate()` to catch missing context data early — it logs `push_error` per missing key with the script path.
   - Use `unique_name_in_owner = true` on nodes referenced via `%NodeName`.
   - Use `call_deferred()` for all navigation operations triggered from signal handlers.
   - Access UIRouter via `get_node("/root/UIRouter")` with a null check.
   - Choose the correct layer for the scene type (see Layer Selection below).
   - Choose the correct InputMode for the scene type (see Input Mode Selection below).

4. **Build the UI layout** inside the UIActivatable scene (delegate to godot-ui-core knowledge):
   - Read `../godot-ui-core/references/ui-decision-table.md` to pick the layout strategy:
     - Anchored for full-screen backgrounds, edge-pinned overlays, or fixed macro-zones.
     - Containers (VBox, HBox, Grid, Margin) for button stacks, forms, lists, localized text.
     - Mixed layout when an anchored region contains dynamic container-driven content.
   - Build the Control tree: add containers, labels, buttons, progress bars, texture rects, etc.
   - Set `custom_minimum_size` on dynamic roots that could collapse to zero.
   - Set `mouse_filter = MOUSE_FILTER_IGNORE` on purely visual frames/backgrounds.
   - Set `mouse_filter = MOUSE_FILTER_STOP` on modal blockers.
   - Set `focus_mode` and `focus_neighbor` for keyboard/gamepad navigation chains.
   - Call `grab_focus()` on the primary interactive control in `activate()`.
   - Use `ScrollContainer.follow_focus = true` when focusable children can scroll out of view.
   - Apply styling through shared `Theme` resources and `theme_type_variation`.
   - Use `TextureRect` instead of `Sprite2D` inside UI trees.
   - Validate layout with `../godot-ui-core/references/layout-checklist.md`.

5. Wire navigation using UIRouter operations:
   - `push_to_layer` / `push_to_layer_with_transition` to open a new screen.
   - `pop_from_layer` / `pop_from_layer_with_transition` to go back.
   - `pop_active()` / `pop_active_with_transition()` to pop whatever is active without knowing its layer — preferred for reusable screens (e.g., settings that can live on MENU or GAME_MENU).
   - `get_layer_for_activatable(self)` to find which layer a screen is on (returns -1 if not found, searches all stacks not just tops).
   - `replace_in_layer` / `replace_in_layer_with_transition` for atomic swap.
   - `clear_layer` / `clear_layer_with_transition` to reset a layer.
   - Always pass context dictionaries with explicit typed values.
   - Use transition variants with appropriate durations (0.2-0.5s typical).
   - UIRouter enforces `max_stack_depth` (default 32) per layer. Hitting this limit means a navigation loop.

   For shader-based transitions (dissolves, wipes, masked fades), use TransitionManager directly:
   - `transition_in(options)` to cover the screen with a shader mask, then `await tm.screen_covered`.
   - `transition_out(options)` to reveal, then `await tm.transition_completed`.
   - Options: `duration`, `mask_texture` (grayscale: white=uniform, gradient=wipe, noise=dissolve), `edge_softness`, `fade_color`, `show_progress_bar`.
   - `update_progress(value)` to drive the progress bar during loading.
   - Transition queue holds up to `max_queue_size` (default 8) operations. `cancel_transition()` kills current tween and clears queue.

6. If the task involves world transitions and SceneRouter is registered:
   - `change_world(scene_path, context, options)` returns `bool` — `false` if rejected (already transitioning, invalid path) or failed mid-sequence, `true` on success.
   - Options dict supports: `fade_out_duration`, `fade_in_duration`, `min_duration_ms`, `show_loading`, `loading_scene`, `loading_context`, `cleanup_policy` (CLEANUP_TRANSIENT/ALL/NONE/CUSTOM), `cleanup_layers` (array for CUSTOM), `use_shader_transition`, `mask_texture`, `edge_softness`, `fade_color`, `show_progress_bar`, `skip_history`, `use_cache`, `transition_context` (TransitionContext resource).
   - Context injection into worlds uses duck typing: `apply_transition_context(ctx)` first (if TransitionContext provided), then `apply_context(context)`, then `activate(context)` as fallback.
   - Scene caching: `preload_scene(path)` for instant future transitions, `evict_cached_scene(path)`, `clear_scene_cache()`, `get_cache_size()`. LRU cache holds 8 entries by default. Cache hits skip the loading phase.
   - History: `can_go_back()`, `go_back()`, `get_history_size()`. Max 32 entries.
   - Restart: `restart_world()` (same context), `restart_world_with_context(new_context)`.
   - Connect to `world_transition_started`, `world_transition_finished`, `world_load_progress` signals.
   - Layer cleanup happens after loading succeeds (not before), so a failed load preserves existing UI state.

7. If the task involves input glyphs:
   - Place `InputGlyph` nodes in scenes, set `action_name` and `glyph_db` exports.
   - Ensure glyph textures exist in `res://ui/glyphs/{device_family}/` directories.
   - InputGlyph auto-updates when InputDeviceTracker detects device changes.

8. If the task involves themes:
   - Use `UIThemeManager.apply_theme(preset_name)` to switch themes.
   - Theme cascades automatically to all UIRouter CanvasLayer children.
   - Register custom themes via `UIThemeManager.register_theme(name, theme)`.

9. Read `references/integration-checklist.md`, then validate:
   - All UIActivatable scenes implement the 5-method contract.
   - `reactivate()` is overridden when activate() does expensive one-time setup.
   - `require_context_keys()` is used when screens depend on specific context data.
   - Context dictionaries use supported types (primitives, Vector2/3, Array, Dictionary, Resource, null).
   - Navigation operations use `call_deferred()` from signal handlers.
   - Layer choices match scene purpose.
   - InputMode choices match scene interaction needs.
   - Back navigation is handled or explicitly delegated to default pop.
   - Transitions do not leave input in LOCKED state after completion.
   - Use `get_debug_state()` or `perform_health_check()` to diagnose issues. Set `log_level = 0` on autoloads for production.

10. Escalate when boundaries are exceeded:
   - Scene/resource ownership or duplication -> godot-scene-resource
   - Save/load or persistence -> godot-persistence
   - Architecture decisions -> godot-architect
   - Reusable process failure -> godot-retro

## Layer Selection

| Scene Type | Layer | Priority | Reason |
|---|---|---|---|
| In-game overlay (health, score, minimap) | GAME_HUD | 0 | Lowest priority, allows gameplay input |
| In-game menu (pause, inventory) | GAME_MENU | 10 | Blocks gameplay, sits above HUD |
| Out-of-game menu (main menu, settings, credits) | MENU | 20 | Standard menu layer |
| Blocking dialog (confirmation, alert) | MODAL | 30 | Blocks all lower layers |
| Loading screen (progress, splash) | LOADING | 40 | Highest priority, blocks everything |

## Input Mode Selection

| Scene Type | InputMode | show_cursor | capture_cursor | Reason |
|---|---|---|---|---|
| HUD overlay | GAME | false | false | Must not block gameplay input |
| Menu / pause / settings | MENU | true | false | Blocks gameplay, shows cursor |
| Modal dialog | LOCKED | true | false | Blocks all input except UI |
| Loading screen | LOCKED | false | false | Blocks all input during load |
| Gameplay with UI (e.g. RTS) | ALL | true | false | Both UI and gameplay active |

## Output

Leave behind:

- UIActivatable `.tscn` and `.gd` files created or changed.
- Navigation wiring code with layer, context, and transition parameters.
- Project surfaces inspected (autoloads, input actions, themes, glyphs).
- Layer and InputMode choices with reasoning.
- Context dictionary shapes documented in code comments.
- Validation performed and limits noted.
- Exact blocker and next action if blocked.

## Quality Gates

- CommonUI autoloads were confirmed present before any framework code was written.
- Every UIActivatable scene implements all 5 contract methods with correct signatures.
- **UI layout uses anchors for macro placement and containers for dynamic content** (no manual positioning under containers).
- **`custom_minimum_size` is set on dynamic roots that could collapse to zero.**
- **Focus chain is configured for keyboard/gamepad navigation on interactive screens.**
- **`mouse_filter` is intentional: IGNORE on visual frames, STOP on modal blockers.**
- `reactivate()` is overridden when `activate()` does expensive one-time setup that should not repeat on uncover.
- `require_context_keys()` is used when screens depend on specific context keys.
- Navigation operations use `call_deferred()` when called from signal handlers.
- Context dictionaries use only supported value types.
- Layer choices match the scene's purpose and interaction model.
- InputMode choices prevent unintended input bleed.
- Back navigation is explicitly handled or intentionally delegated.
- Reusable screens use `pop_active()` or `get_layer_for_activatable()` instead of hardcoding layers.
- Shader transitions use appropriate mask textures and await `screen_covered` / `transition_completed`.
- SceneRouter `change_world()` return value is checked for failure.
- Transitions complete cleanly without leaving input locked.
- No direct UIRouter singleton calls from UIActivatable scenes' contract methods (use deferred private methods).

## Failure Modes

- Do not create UI scenes that skip the UIActivatable contract. Every navigable scene must extend UIActivatable and implement all 5 methods.
- Do not call UIRouter navigation methods directly inside signal handlers. Always use `call_deferred()`. The framework defers `remove_child` internally so it no longer crashes, but deferring navigation is still the safe pattern.
- Do not use `:=` type inference with `Dictionary.get()`. Always use explicit type annotation.
- Do not put `class_name` on autoload scripts.
- Do not assume UIRouter exists without checking `get_node("/root/UIRouter")` for null.
- Do not use GAME InputMode for menus or MENU InputMode for HUD overlays.
- Do not pass unsupported types (Object, Node references) in context dictionaries.
- Do not leave TransitionManager in a transitioning state without completion.
- Do not create UIActivatable scenes without a full-rect Control root.
- Do not skip `ui_cancel` input action setup when back navigation is expected.
- Do not use this skill when the project lacks CommonUI autoloads.
- Do not forget that `reactivate()` is called on uncover, not `activate()`. If `activate()` does expensive one-time setup, override `reactivate()` for the cheap refresh path. If you do not override `reactivate()`, `activate()` must be idempotent.
- Do not hardcode layer enums in reusable screens. Use `pop_active()` or `get_layer_for_activatable(self)` instead.
- Do not set gradient/shader transition mode without assigning a mask texture — it falls back to uniform fade silently.
- Do not spam navigation with transitions faster than they complete — the queue holds 8 operations max (configurable via `max_queue_size`), excess is dropped.
- Do not call `change_world()` while a transition is in progress — it returns `false` immediately. Check `is_transitioning()` or use the return value.
- Do not manually clear the LOADING layer during SceneRouter transitions — SceneRouter manages it. No cleanup policy ever clears LOADING.
- Do not rely on theme properties in `_ready()` — UIThemeManager applies themes via `call_deferred()` on startup, so they are not set until the next frame. Use `activate()` for theme-dependent setup.
- Do not call `queue_free()` directly on managed UIActivatables — use `pop_from_layer()` or `clear_layer()`. Direct freeing skips the deactivation lifecycle (though the framework auto-cleans tracking state via `tree_exiting`).
- Do not override `mouse_filter` in `deactivate()` or `activate()` without understanding that UIRouter sets covered screens to `MOUSE_FILTER_IGNORE` and restores to `MOUSE_FILTER_STOP` on uncover.

## References

Read only as needed:

- `references/framework-api.md` -- Core API reference for UIRouter, UIInputRouter, TransitionManager, SceneRouter, UIThemeManager, InputGlyphDB, InputGlyph
- `references/scene-templates.md` -- Concrete templates for each UIActivatable scene type with .tscn and .gd patterns
- `references/integration-checklist.md` -- Pre-flight and post-flight validation checklist
- `../godot-ui-core/references/ui-decision-table.md` -- Layout strategy decisions (anchors vs containers vs mixed)
- `../godot-ui-core/references/layout-checklist.md` -- Layout validation checklist (resolution safety, size flags, focus)
- `../godot-ui-core/references/examples-and-validation.md` -- Concrete UI layout examples and validation patterns
