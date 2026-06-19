# Examples and Validation

Use this file before finalizing UI work.

## Example Input

Task:

- add a pause menu and game-over flow for an existing project
- keep gameplay frozen while the menu still responds
- preserve mouse and gamepad usability
- do not redesign unrelated scenes or gameplay logic

## Good Output Shape

```md
- files changed:
  - res://scenes/ui/PauseMenu.tscn
  - res://scenes/ui/GameOverScreen.tscn
  - res://scripts/ui/pause_menu.gd
  - res://themes/ui_theme.tres
- project surfaces inspected:
  - project.godot
  - scenes/main/Game.tscn
  - scripts/game_manager.gd
  - input actions: pause, ui_accept, ui_cancel
- MCP actions performed:
  - `open_scene` on `res://scenes/main/Game.tscn`
  - `scene://current/tree` read before mutation
  - `add_node` for the pause root, dimmer, and button stack
  - `update_property` for `mouse_filter`, `process_mode`, and focus setup
  - `set_anchor_preset` for full-rect and centered layout nodes
  - `connect_signal` for button actions
  - `save_scene`
- layout strategy:
  - CanvasLayer host for overlays
  - full-rect ColorRect blocker with `mouse_filter = MOUSE_FILTER_STOP`
  - CenterContainer plus VBoxContainer for menu actions
- focus and pause rules:
  - `process_mode = PROCESS_MODE_WHEN_PAUSED`
  - `_ready()` grabs focus on Resume
  - static labels use `FOCUS_NONE`
  - retry and main-menu actions clear `get_tree().paused`
- theme strategy:
  - shared Theme resource with `DangerButton` for quit-like actions
- validation performed:
  - `scene://current/tree` re-read confirms the modal hierarchy and named nodes after save
- limits:
  - resize behavior, runtime pause behavior, and gamepad navigation still require stronger in-editor or runtime validation if they were not exercised
```

## Reference Blueprints

### Responsive Main Menu

```text
Control (full rect)
|- TextureRect background (full rect, expand keep size, ignore)
`- MarginContainer (full rect)
   `- CenterContainer
      `- VBoxContainer
         |- Label title (focus none)
         |- Button play
         |- Button options
         `- Button quit (theme_type_variation = "DangerButton")
```

Checks:

- `grab_focus()` targets the primary action.
- Button stack uses container spacing and explicit minimum width where needed.
- Background art scales through `TextureRect` rather than `Sprite2D`.

### Safe HUD Layering

```text
CanvasLayer
`- Control (full rect, mouse_filter = ignore)
   `- MarginContainer (full rect, mouse_filter = ignore)
      |- VBoxContainer top_left_stats
      `- HBoxContainer top_right_resources
```

Checks:

- Non-interactive wrappers use `MOUSE_FILTER_IGNORE`.
- Actual interactive controls opt into their own input handling.
- HUD update logic reacts to signals or explicit setters instead of polling every frame.

### Pausable Modal Window

```text
CanvasLayer or Control root (process_mode = when_paused)
`- ColorRect dimmer (full rect, mouse_filter = stop)
   `- CenterContainer
      `- PanelContainer
         `- VBoxContainer
            |- Label heading (focus none)
            |- Button resume
            |- Button options
            `- Button quit
```

Checks:

- The dimmer blocks gameplay clicks.
- The modal root keeps processing while the rest of the scene tree is paused.
- Exiting the flow unpauses before changing scenes or retrying.

## Validation Ladder

- Editor-connected:
  - `open_scene` on the target scene and read `scene://current/tree` before mutation
  - confirm the saved hierarchy, anchor presets, mouse filters, focus rules, and signal wiring by re-reading `scene://current/tree`
  - run the scene and resize between wide, standard, and tall aspect ratios when the task allows runtime validation
  - verify cold-start keyboard or gamepad focus without touching the mouse
  - confirm HUD click-through, modal click blocking, pause freeze behavior, and retry unpause behavior
  - inspect scrollable lists if present and verify focus remains visible
- Filesystem-only:
  - verify the host node, layout structure, mouse filters, focus setup, theme usage, and pause-reset logic in scene and script files only when the task explicitly permits file-only work
  - confirm anchors versus containers match the intended UI behavior
  - end with the exact MCP or runtime validation step still required
- Blocked:
  - stop if the destination scene, input actions, UI host, or allowed output location is unknown
  - stop if a new scene file is required but the task does not permit creating a starter `.tscn` before `open_scene`

## Checklist

- Actual target scenes, scripts, and display settings were inspected before choosing a layout pattern.
- Editor-connected scene work names the `scene://current/tree` read plus the MCP tool sequence used.
- Overlays that should not block gameplay use `MOUSE_FILTER_IGNORE`.
- Modals that must shield gameplay use `MOUSE_FILTER_STOP`.
- Static text does not accidentally take focus.
- Menu or modal roots assign initial focus intentionally.
- Dynamic panels define `custom_minimum_size` when collapse risk exists.
- Theme changes use a shared `Theme` or type variation when the style repeats.
- Pause or game-over flows explicitly clear `get_tree().paused` before leaving the frozen scene.
- Validation claims match the real operating mode.

## Scenario Checks

### Title Menu

- Resize to portrait-like and ultrawide shapes.
- Confirm the title stays centered and button widths do not collapse below their intended minimums.
- Confirm background art crops or expands acceptably instead of distorting.

### HUD Overlay

- Click empty HUD space over active gameplay.
- Confirm world input still reaches the intended gameplay handler.
- Confirm decorative shells are not the nodes consuming the event.

### Pause Modal

- Pause during active physics or particle activity.
- Confirm the game world freezes while the modal still accepts navigation.
- Confirm the dimmer blocks clicks into the frozen scene behind it.

### Game Over and Retry

- Trigger the game-over screen from a paused state.
- Confirm retry or main-menu flow clears the paused state before scene reload or scene change.
- Confirm initial focus lands on the expected action.

### Scrollable Menus

- Navigate a long list with keyboard or gamepad.
- Confirm focus never disappears off-screen without the container following it.
- Confirm wrap or edge behavior is explicit rather than accidental.

## Common Failures

- Individually anchored buttons drift or overlap when text length changes.
- A full-screen HUD wrapper consumes clicks that should hit gameplay.
- `MOUSE_FILTER_PASS` is used where true click-through is required.
- A modal unpauses too late and the next scene loads frozen.
- Static labels or rich text steal focus from buttons.
- An instanced panel stays invisible because its root never got a minimum size.
- The task edits an existing UI scene on disk and never reconciles the editor-owned hierarchy through MCP.
- The final note claims editor validation that never happened.

## Escalate

- Scope is expanding into broader UX planning or product direction -> `godot-scope`
- The task now depends on project-level architecture or display-strategy decisions -> `godot-architect`
- Scene or resource boundaries are risky -> `godot-scene-resource`
- Save/load implications become part of the UI flow -> `godot-persistence`
- The UI workflow itself needs reusable correction -> `godot-retro`
