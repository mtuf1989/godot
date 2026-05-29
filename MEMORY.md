# MEMORY.md — Symphony Project

## Active Decisions

- GraphFrame title is single-line. Comment nodes use Inspector for title editing, not inline TextEdit.
- Bottom panel plugins must guard `make_visible(false)` — return early if the edited resource is still valid. Godot's editor calls `edit(null)` + `make_visible(false)` when focus leaves the resource, even for clicks inside the plugin's own panel.
- Undo/redo param changes must use `commit_action()` (execute=true), not `commit_action(false)`. The do-method is what actually updates the graph description.

## Proven Patterns

- [Phase 4.A] Use `_param_row` meta tag on HBoxContainer children to identify param rows for collapse/SpinBox lookup. Don't rely on child index arithmetic — it breaks when row types (pin rows vs param rows) share the same container class.
- [Phase 4.A] For `EditorUndoRedoManager` integration in a module: bind helper methods via `ClassDB::bind_method` in `_bind_methods()`, then reference them by string name in `add_do_method`/`add_undo_method`. The methods must accept Variant-compatible types.
- [Phase 4.A] `MERGE_ENDS` merge mode on undo actions is appropriate for continuous changes (slider drags, position moves) — it collapses rapid sequential changes into one undo step.
- [Phase 4.A] Inspector proxy pattern: create an Object subclass with dynamic `_get_property_list`/`_get`/`_set` to expose arbitrary data in Godot's Inspector. Call `push_item(proxy, "", true)` with `p_inspector_only=true`.
- [Phase 4.A] Static class members work for cross-instance clipboard in Godot modules (e.g., `static ClipboardData clipboard` for copy/paste across graph instances).

## Known Quirks And Errors

- `push_item()` triggers the editor plugin system to re-evaluate `handles()` on all plugins. If your plugin doesn't handle the pushed object, `make_visible(false)` is called and your panel hides. Fix: guard `make_visible(false)` with a check that the edited resource is still valid.
- `EditorUndoRedoManager::is_committing_action()` returns true during the entire `commit_action()` call, including when the do-method triggers property changes that re-enter `_set`. This makes it impossible to use undo/redo from Inspector `_set` during a commit. Fix: bypass undo/redo in Inspector `_set` and call the update method directly.
- `vformat` with `%d` does not accept `bool` or pointer types in Godot 4.6 — use explicit casts or `%s` with String conversion.
- GraphEdit's `node_selected` signal fires for both `GraphNode` and `GraphFrame` selections. Cast to determine which type was selected.
- `GraphEdit::set_connection_lines_thickness()` is global only — no per-connection thickness API exists. Use slot colors for type differentiation instead.
- `SpinBox::set_value_no_signal()` (inherited from Range) updates the value without firing `value_changed`. Use this when programmatically updating SpinBoxes to avoid re-entrant signal loops.
- `TextEdit::set_placeholder()` not `set_placeholder_text()` in Godot 4.6.
- `ClassDB` requires `#include "core/object/class_db.h"` in the .cpp file — the forward declaration in object.h is not sufficient for calling `ClassDB::bind_method`.

## Superseded Decisions

(none yet)
