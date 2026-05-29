---
name: godot-ui-core
description: |
  REFERENCE SKILL — Layout construction knowledge for Godot 4 Control trees. This skill provides the rules,
  decision tables, and checklists for building resolution-safe UI layouts with anchors, containers, themes,
  focus chains, and input routing.

  In normal workflow, godot-common-ui is the entry point for ALL UI work and delegates to this skill's
  references for layout decisions. This skill should only trigger DIRECTLY when:
  - The project does NOT have CommonUI autoloads and needs standalone Control layout work.
  - The user explicitly asks about anchors-vs-containers theory, Control layout debugging, or theme styling
    in isolation from any screen management or navigation.
  - Another skill needs to reference layout construction rules without the full CommonUI workflow.

  For any task that involves creating a screen, menu, HUD, popup, dialog, or any navigable UI element,
  use godot-common-ui instead — it incorporates this skill's layout knowledge automatically.
---

# Godot UI Core (Reference Skill)

Layout construction knowledge for Control-based UI. Provides rules for anchors, containers, themes, focus, input routing, and resolution safety.

**Primary consumer: godot-common-ui** — which reads this skill's references during its layout construction step (workflow step 4).

Read `references/ui-decision-table.md` first. Read `references/examples-and-validation.md` before finalizing the result. Read `references/layout-checklist.md` when validating or debugging.

## Responsibility

- Inspect the actual project surfaces the UI touches before editing:
  - `project.godot` display and scaling settings
  - target scenes and nearby integration points
  - current UI host nodes such as `CanvasLayer`, `Control`, or autoloaded transition layers
  - input actions, focus/navigation assumptions, and existing theme resources
  - desktop, mobile-like, pixel-art, or high-DPI constraints already implied by the project
- Create or update bounded UI scenes, scripts, and theme resources for:
  - HUD overlays
  - main or settings menus
  - pause modals
  - game-over screens
  - simple screen-flow transitions
- Enforce reliable Control-tree rules:
  - anchors for viewport or fixed-bound placement
  - containers for dynamic or localized content
  - mixed layouts when a pinned zone contains dynamic children
  - `TextureRect` instead of `Node2D` or `Sprite2D` inside UI trees
  - intentional `custom_minimum_size`, size flags, mouse filters, focus rules, and pause modes
- Prefer theme resources and `theme_type_variation` for repeated styling instead of scattered local overrides.
- When editor-connected, drive scene-tree mutation through MCP scene tools and use direct file edits only for supporting scripts, themes, or starter assets.
- Validate with the strongest honest method available and report limits without bluffing.

## Use When

- The project does NOT have CommonUI autoloads and needs standalone Control layout work.
- The user explicitly asks about anchors-vs-containers theory or Control layout debugging in isolation.
- Another skill needs to reference layout construction rules without the full CommonUI workflow.
- Pure theme styling or resolution-scaling questions with no navigation component.

## Do Not Use When

- The project has CommonUI autoloads and the task involves creating any screen, menu, HUD, popup, or navigable UI. Use **godot-common-ui** — it incorporates this skill's layout knowledge automatically.
- The task involves screen navigation, transitions, popups, level changes, or any managed UI flow. Use **godot-common-ui**.
- Scope is still broad product design, UX direction, or feature planning.
- The main problem is gameplay logic rather than UI structure or integration.
- The task is primarily about 2D art production, shader/VFX polish, or 3D world-space presentation.
- Scene/resource ownership, save/load identity, or higher-level architecture is still unresolved.

## Workflow

1. Read `references/ui-decision-table.md`, then inspect the real project artifacts involved:
   - `project.godot` display settings such as base size, `content_scale_mode`, `content_scale_aspect`, and any pixel-art stretch expectations
   - the exact scene or autoload receiving the UI
   - existing theme resources, UI scenes, and scripts that set conventions
   - input actions and current mouse, keyboard, or gamepad navigation behavior
2. Confirm preconditions before editing:
   - the target scene or folder is known
   - a valid UI host exists, or the task explicitly allows adding one
   - if a brand-new scene file is required, the task explicitly allows creating a starter `.tscn` on disk before opening it in the editor
   - the requested work fits HUD, menu, modal, or screen-flow boundaries
3. Pick the layout strategy from `references/ui-decision-table.md`:
   - anchored for full-screen backgrounds, edge-pinned overlays, or fixed macro-zones
   - containers for button stacks, forms, grids, lists, and localized text
   - mixed layout when an anchored region contains dynamic container-driven content
   - avoid over-nesting and assign `custom_minimum_size` for dynamic roots that could collapse to zero
4. When the task touches editor-owned scene structure, use MCP in this order:
   - `open_scene` for the target scene
   - `scene://current/tree` to inspect the existing hierarchy before mutating it, or `find_nodes` for targeted lookups by name, type, or group when the full tree is not needed
   - `add_node` for new `CanvasLayer`, `Control`, `Container`, or interactive child nodes
   - `duplicate_node` to clone existing UI structures (buttons, panels) instead of recreating them manually
   - `update_property` for names, size flags, `mouse_filter`, `focus_mode`, `process_mode`, scripts, theme references, and other Control properties
   - `set_anchor_preset` for full-rect or pinned layout roots
   - `connect_signal` or `disconnect_signal` when the task includes button wiring or flow changes
   - `move_node` only when reparenting is required
5. Make the smallest coherent change with these defaults unless grounded project facts require otherwise:
   - use `CanvasLayer` for screen-space overlays that must ignore world camera movement
   - use a full-rect `Control`, `MarginContainer`, `ColorRect`, or `TextureRect` as the top layout frame
   - set `mouse_filter = MOUSE_FILTER_IGNORE` on purely visual HUD shells or background frames that should not block gameplay input
   - set `mouse_filter = MOUSE_FILTER_STOP` on modal blockers that must shield paused or hidden gameplay
   - call `grab_focus()` on the primary interactive control for cold-start keyboard or gamepad support
   - set `focus_mode = FOCUS_NONE` on non-interactive text unless accessibility metadata is intentionally being configured
   - use `ScrollContainer.follow_focus = true` when focusable children can scroll out of view
   - set pause UI roots to `PROCESS_MODE_WHEN_PAUSED` or `PROCESS_MODE_ALWAYS`
   - clear `get_tree().paused` before retrying or leaving a paused scene
6. Apply styling through shared `Theme` resources and `theme_type_variation`. Use local overrides only for isolated runtime state that should not become a reusable style rule. Edit supporting `.gd`, `.tres`, or starter `.tscn` files directly on disk when that is more efficient, then call `refresh_filesystem` before relying on the editor state.
7. If scene mutation happened through MCP, call `save_scene` after the structure and property changes are complete.
8. Read `references/examples-and-validation.md` and `references/layout-checklist.md`, then validate with the strongest honest method available:
   - in editor-connected mode, re-read `scene://current/tree` to confirm the expected UI hierarchy, then run any stronger in-editor or runtime checks the task allows
   - `editor_screenshot` after `run_scene` to visually verify UI layout, scaling, and overlay behavior in the running game
   - aspect ratio and display-scale stress
   - HUD click-through versus modal click blocking
   - cold-start focus and wrap behavior
   - pause freeze versus UI responsiveness
   - scene retry or transition unpause safety
9. Escalate instead of improvising:
   - ANY task that needs screen management, navigation, popups, transitions, or level changes -> **godot-common-ui** (this skill is a reference; godot-common-ui is the executor)
   - broad scope or unresolved UX direction -> `godot-scope`
   - unresolved architecture or project-level display strategy -> `godot-architect`
   - risky scene or resource ownership questions -> `godot-scene-resource`
   - save/load identity concerns in menu or game-over flow -> `godot-persistence`
   - shader-driven UI polish as the main work -> `godot-architect` until a dedicated shader UI skill exists
   - reusable process failure in this skill -> `godot-retro`

## Output

Leave behind bounded UI work with:

- files, scenes, or resources created or changed
- project surfaces inspected before editing
- MCP resources, tools, and sync steps used when editor-connected
- layout strategy used and why
- input, focus, pause, and theme rules applied
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- Display settings and concrete target scenes were inspected before layout decisions were made.
- Editor-connected scene mutation uses MCP scene tools and scene-tree reads instead of blind edits to an existing `.tscn`.
- No `Node2D` or `Sprite2D` is embedded inside the UI layout tree when a `Control`-based node is required.
- Anchors are used for macro placement and containers are used for dynamic content.
- `custom_minimum_size` and size flags are explicit where collapse or shrinkage would otherwise break the UI.
- Passive overlays do not block gameplay input, while modals intentionally do.
- Mouse, keyboard, and gamepad paths remain coherent, with explicit initial focus where relevant.
- Pause-aware flows do not leave the next scene frozen.
- Styling uses shared theme resources or type variations instead of repeated local overrides.
- Validation claims match the actual operating mode.

## Failure Modes

- Do not anchor localized button stacks, lists, or text-heavy panels node by node.
- Do not manually position children under containers and expect the layout to hold.
- Do not let dynamically instanced UI roots stay at the default zero-size state.
- Do not use `MOUSE_FILTER_PASS` as a substitute for true gameplay click-through.
- Do not allow static labels or rich text to hijack gamepad focus unintentionally.
- Do not keep adding one-off theme overrides when the style should be shared.
- Do not edit an existing UI scene blindly on disk when the task expects editor-owned scene mutation through MCP.
- Do not claim editor or runtime validation when only filesystem inspection was performed.
- Do not widen the task into general gameplay coding, art creation, or a full UX redesign.

## References

Read only as needed:

- `references/ui-decision-table.md`
- `references/examples-and-validation.md`
- `references/layout-checklist.md`
- `../../foundation/Godot Nuanced Development Practices.md`
