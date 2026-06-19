# Layout Checklist

Use this checklist when validating or debugging a Godot UI task.

## Grounding

- `project.godot` display settings were inspected before changing layout assumptions.
- The exact scene, script, or autoload hosting the UI was inspected.
- Existing theme resources, input actions, and UI folder conventions were identified.

## Host and Scale

- Screen-space overlays use `CanvasLayer` when they must ignore world camera movement.
- The top layout frame is full-rect when it should cover the viewport.
- Modern UI keeps crisp text through the grounded display strategy, usually `content_scale_mode = canvas_items`.
- Pixel-art-specific stretch assumptions were inspected instead of guessed.

## Layout

- Anchors are used for viewport or fixed-bound placement, not for text-heavy stacks or dynamic lists.
- Containers own layout for menus, forms, lists, and grids.
- Mixed layouts keep overlap or corner pinning local and intentional.
- No `Node2D` or `Sprite2D` is embedded inside the Control layout tree when `TextureRect` is the correct UI node.
- Dynamic or instanced panels define `custom_minimum_size` where zero-size collapse is possible.

## Input

- Purely visual HUD shells use `MOUSE_FILTER_IGNORE`.
- Modal blockers use `MOUSE_FILTER_STOP`.
- `MOUSE_FILTER_PASS` is used only for UI-to-UI propagation, not for gameplay click-through.
- Overlapping parents or siblings are not shadowing buttons unintentionally.

## Focus and Navigation

- The primary action grabs focus on scene entry when keyboard or gamepad use matters.
- Static labels and decorative text use `FOCUS_NONE` unless accessibility behavior is intentional.
- Focus neighbors are explicit where wrap behavior matters.
- `ScrollContainer.follow_focus` is enabled when focused children can leave the visible viewport.

## Pause and Screen Flow

- Pause UI roots use `PROCESS_MODE_WHEN_PAUSED` or `PROCESS_MODE_ALWAYS` intentionally.
- Gameplay nodes that should freeze still inherit the paused scene-tree behavior.
- Retry, continue, or main-menu exits clear `get_tree().paused` before scene reload or scene change.
- Transition layers that must survive scene replacement are hosted safely, usually in an autoloaded `CanvasLayer`.

## Theme

- Repeated styling lives in a shared `Theme` resource.
- Reusable variants use `theme_type_variation`.
- Local overrides are limited to isolated runtime state or true one-off cases.

## Validation

- The UI was checked against wide, standard, and tall aspect ratios when relevant.
- Mouse, keyboard, and gamepad behavior were validated at the strongest honest level available.
- The final report states exactly which validation level was reached and what still needs editor confirmation.
