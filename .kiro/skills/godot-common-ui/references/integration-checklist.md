# CommonUI Integration Checklist

Use this checklist before and after creating or modifying UIActivatable scenes.

## Pre-Flight: Project Readiness

- [ ] `project.godot` registers these autoloads in order:
  1. UIRouter -> `res://autoload/ui_router.gd`
  2. UIInputRouter -> `res://autoload/ui_input_router.gd`
  3. TransitionManager -> `res://autoload/transition_manager.gd`
  4. SceneRouter -> `res://autoload/scene_router.gd` (if world transitions needed)
  5. UIThemeManager -> `res://autoload/ui_theme_manager.gd` (if theme switching needed)
- [ ] `ui_cancel` input action exists in InputMap (Escape key + Gamepad B button)
- [ ] Framework scripts exist in `scripts/` directory:
  - `ui_activatable.gd`, `ui_layer.gd`, `ui_input_config.gd`, `deactivate_reason.gd`
  - `device_type.gd`, `input_glyph_db.gd`, `input_glyph.gd` (if glyphs needed)
- [ ] Framework autoload scripts exist in `autoload/` directory

## UIActivatable Contract Compliance

Every UIActivatable scene must pass all of these:

- [ ] Script extends `UIActivatable`
- [ ] `activate(context: Dictionary) -> void` is implemented
  - Sets `visible = true`
  - Reads context with explicit types (no `:=` with `Dictionary.get()`)
- [ ] `deactivate(reason: DeactivateReason.Reason) -> void` is implemented
  - Sets `visible = false`
  - Handles cleanup appropriate to the reason
- [ ] `get_desired_focus_target() -> Control` is implemented
  - Returns a focusable Control for menus/dialogs
  - Returns null for HUD overlays that must not steal focus
- [ ] `get_desired_input_config() -> UIInputConfig` is implemented
  - Returns correct InputMode for the scene type
- [ ] `on_back_requested() -> bool` is implemented
  - Returns true if the scene handled back navigation
  - Returns false to let the system pop automatically

## Scene Structure

- [ ] Root node is `Control` with `anchors_preset = 15` (FULL_RECT)
- [ ] Root node has `layout_mode = 3` (anchor-based)
- [ ] Interactive nodes use `unique_name_in_owner = true` for `%Name` access
- [ ] Script references use `@onready var name: Type = %Name` pattern
- [ ] Null checks on `@onready` vars before use (nodes may not exist in all configurations)

## Navigation Safety

- [ ] All UIRouter calls from signal handlers use `call_deferred()`
- [ ] UIRouter access uses `get_node("/root/UIRouter")` with null check
- [ ] Context dictionaries use only supported types:
  - `bool`, `int`, `float`, `String`
  - `Vector2`, `Vector3`
  - `Array`, `Dictionary`
  - `Resource` (and subclasses)
  - `null`
- [ ] No `Node`, `Object`, or `Callable` values in context dictionaries
- [ ] Layer choice matches scene purpose (see Layer Selection in SKILL.md)
- [ ] Transition durations are reasonable (0.15-0.5s typical)

## Input Configuration

- [ ] HUD overlays use `InputMode.GAME` with `show_cursor = false`
- [ ] Menus use `InputMode.MENU` with `show_cursor = true`
- [ ] Modal dialogs use `InputMode.LOCKED` with `show_cursor = true`
- [ ] Loading screens use `InputMode.LOCKED` with `show_cursor = false`
- [ ] No scene uses GAME mode when it should block gameplay input
- [ ] No scene uses LOCKED mode when it should allow some input

## Focus Management

- [ ] Menu scenes return a specific Button as focus target
- [ ] Modal dialogs return the cancel/safe button as focus target
- [ ] HUD overlays return null (no focus stealing)
- [ ] Loading screens return null (no interaction)
- [ ] Focus targets are actually focusable Controls (Button, LineEdit, etc.)

## Back Navigation

- [ ] Main menu: back shows quit confirmation (returns true)
- [ ] Pause menu: back resumes game (returns true)
- [ ] Settings: back cancels/saves and pops (returns true)
- [ ] Modal dialog: back cancels dialog (returns true)
- [ ] Loading screen: back is blocked (returns true)
- [ ] Simple sub-screens: back pops (returns false for auto-pop)

## Typing Rules

- [ ] All variables have explicit type annotations
- [ ] All function parameters have explicit types
- [ ] All return types are declared
- [ ] No `:=` with `Dictionary.get()` -- use `var x: Type = dict.get("key", default)`
- [ ] No `class_name` on autoload scripts

## Post-Flight: Validation

- [ ] Scene loads without errors
- [ ] All 5 contract methods execute without crashes
- [ ] Navigation push/pop/replace works correctly
- [ ] Context data arrives in `activate()` as expected
- [ ] Back navigation (Escape/B button) behaves correctly
- [ ] Transitions complete without leaving input locked
- [ ] Focus lands on the correct control after navigation
- [ ] Layer priority blocking works (higher layers block lower)

## Common Mistakes to Avoid

1. Calling `UIRouter.push_to_layer()` directly in a `pressed` signal handler instead of using `call_deferred()`.
2. Using `:=` with `context.get("key", default)` which causes type inference issues.
3. Forgetting to set `visible = true` in `activate()` and `visible = false` in `deactivate()`.
4. Returning the wrong value from `on_back_requested()` (true = handled, false = auto-pop).
5. Using GAME InputMode for a menu that should block gameplay.
6. Passing Node references in context dictionaries (they become invalid after scene changes).
7. Forgetting to clear the LOADING layer before pushing the next scene after loading completes.
8. Not checking if UIRouter exists before calling navigation methods.
9. Using `replace_in_layer` when `push_to_layer` is needed (replace destroys the current top).
10. Forgetting `unique_name_in_owner = true` on nodes accessed via `%Name`.
