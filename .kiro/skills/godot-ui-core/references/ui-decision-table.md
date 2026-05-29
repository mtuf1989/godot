# UI Decision Table

Use this file before choosing the Control-tree structure or input behavior.

## Layout Strategy Matrix

| Need | Prefer | Why | Avoid |
| :---- | :---- | :---- | :---- |
| Full-screen background, dimmer, or root safe-area frame | Anchored full-rect `Control`, `ColorRect`, `TextureRect`, or `MarginContainer` | The node must track the viewport or a fixed parent boundary directly. | Building the whole screen out of individually anchored labels and buttons. |
| Corner-pinned HUD cluster with changing child content | Mixed layout: anchored zone plus internal `HBoxContainer` or `VBoxContainer` | The macro position stays fixed while the children can grow or shrink safely. | Manual child offsets inside the changing HUD cluster. |
| Main menu, settings form, inventory grid, dialogue option list | Containers such as `CenterContainer`, `MarginContainer`, `VBoxContainer`, `HBoxContainer`, or `GridContainer` | Text length, localization, and spacing changes are safer when the parent owns layout. | Treating each row or button as an independently anchored node. |
| Overlap case such as a badge over an icon corner | Small anchored sublayout inside a non-container `Control`, then containers for non-overlapping content | Containers do not support arbitrary overlap well, but overlap should stay local and intentional. | Forcing overlap inside a pure container chain. |
| Dynamic tooltip, popup, dialogue box, or instanced panel | Container-driven root with explicit `custom_minimum_size` | Instanced UI often collapses without a minimum size during the first layout pass. | Relying on the default `(0, 0)` size or tweening `size` under a container. |
| UI icons or decorative images inside Control trees | `TextureRect` | It obeys Control layout and scaling rules. | `Node2D` or `Sprite2D` inside the UI hierarchy. |

## Input Consumption Matrix

| `mouse_filter` | Use For | Behavior | Avoid |
| :---- | :---- | :---- | :---- |
| `MOUSE_FILTER_STOP` | Modal dimmers, pause blockers, controls that must fully own the click | Consumes the event so gameplay or lower UI layers cannot react. | Placing it on broad HUD backgrounds that should let gameplay input through. |
| `MOUSE_FILTER_PASS` | Controls that need local hover or click handling while still allowing parent UI controls to participate | Keeps the event within the UI chain. This is still not true click-through to the game world. | Using it to solve gameplay input bleed or hidden overlap bugs. |
| `MOUSE_FILTER_IGNORE` | Purely visual HUD roots, decorative frames, non-interactive overlays | The node does not participate in mouse handling, so lower layers can receive the event. | Interactive buttons, sliders, or modal blockers. |

## Theme Architecture Matrix

| Need | Prefer | Why | Avoid |
| :---- | :---- | :---- | :---- |
| Project-wide baseline fonts, colors, styleboxes, and spacing | Shared `Theme` resource | One change propagates cleanly across the UI tree. | Repeating the same overrides in many scenes or scripts. |
| Reusable variant such as destructive, warning, or compact buttons | `theme_type_variation` such as `DangerButton` | Repeated variants stay centralized and maintainable. | Copying node-local overrides to every matching control. |
| One-off runtime visual state tied to live data | Local override on the specific node | A temporary or truly unique visual state should stay local. | Turning a whole style system into ad hoc runtime overrides. |
| Shared padding or layout constants across multiple screens | Theme constants or one reusable scene pattern | The convention stays consistent and easier to audit. | Re-implementing the same spacing values in multiple scripts. |

## Focus, Pause, and Scaling Quick Rules

- Call `grab_focus()` on the first primary action in menus and modals so keyboard or gamepad input works from a cold start.
- Set `focus_mode = FOCUS_NONE` on static labels and decorative text unless accessibility behavior is intentional.
- Add explicit focus neighbors when a button stack should wrap instead of dead-end.
- Set `ScrollContainer.follow_focus = true` when hidden children can otherwise trap keyboard or gamepad navigation.
- Use `PROCESS_MODE_WHEN_PAUSED` or `PROCESS_MODE_ALWAYS` for pause UI roots that must stay responsive while gameplay freezes.
- Reset `get_tree().paused = false` before retrying or leaving a paused scene.
- For modern crisp UI, prefer `content_scale_mode = canvas_items` and `content_scale_aspect = expand` unless grounded project facts require a different display model.
- For pixel-art-specific UI, inspect whether integer stretch is already required before changing scale settings.
