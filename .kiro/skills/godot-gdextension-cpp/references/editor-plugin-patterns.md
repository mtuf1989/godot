# Editor Plugin Patterns (Godot 4.6 Modules)

Lessons learned from building the Symphony editor plugin as a Godot module with bottom panel, GraphEdit, undo/redo, and Inspector integration.

## Bottom Panel Lifecycle

The editor calls `make_visible(false)` + `edit(null)` on your plugin whenever focus leaves the edited resource — including clicks **inside your own bottom panel**. This is because the editor re-evaluates plugin ownership on any selection change.

**Fix:** Guard `make_visible(false)`:
```cpp
void MyPlugin::make_visible(bool p_visible) {
    if (!p_visible && editor->get_edited_resource().is_valid()) {
        return; // Don't hide while actively editing
    }
    // ... normal show/hide logic
}
```

## EditorUndoRedoManager Integration

### Binding helper methods
Undo/redo methods must be bound via `ClassDB::bind_method` so the undo system can call them by name:
```cpp
ClassDB::bind_method(D_METHOD("_ur_add_node", "data"), &MyEditor::_ur_add_node);
```

### commit_action(true) vs commit_action(false)
- `commit_action()` or `commit_action(true)` — executes the do-method immediately. Use for operations where the UI hasn't been updated yet.
- `commit_action(false)` — skips do-method execution. Use ONLY when the UI is already in the correct state (e.g., node already moved visually by drag).

**Common mistake:** Using `commit_action(false)` for param changes from SpinBox. The SpinBox fires `value_changed` before the graph description is updated, so the do-method MUST execute to update the data model.

### MERGE_ENDS for continuous changes
Use `UndoRedo::MERGE_ENDS` for slider drags, position moves, and other rapid sequential changes. This collapses them into one undo step.

### Re-entrancy during commit
`is_committing_action()` returns true during the entire `commit_action()` call. If your do-method triggers property changes that re-enter `_set` on a proxy object, the re-entrant call will be blocked. **Don't use undo/redo from Inspector `_set` if the do-method can trigger Inspector refresh.** Call the update method directly instead.

## Inspector Proxy Pattern

To show custom data in Godot's Inspector without a full `EditorInspectorPlugin`:

1. Create an `Object` subclass with dynamic `_get_property_list`/`_get`/`_set`
2. Call `EditorNode::get_singleton()->push_item(proxy, "", true)` with `p_inspector_only=true`
3. The proxy's `_set` should call your editor's update methods directly (not through undo/redo) to avoid re-entrancy

**Caveat:** `push_item` triggers plugin re-evaluation. Your `make_visible(false)` guard handles this.

## GraphNode Child Lookup

Don't use child index arithmetic to find SpinBoxes or specific rows. Pin rows and param rows may share the same container type (HBoxContainer). Instead, use `set_meta("_param_row", true)` on param rows and iterate children checking `has_meta("_param_row")`.

## GraphEdit Limitations (4.6)

- `set_connection_lines_thickness()` is global only — no per-connection thickness
- `GraphFrame` title is single-line (Label without autowrap)
- `node_selected` signal fires for both `GraphNode` and `GraphFrame` — cast to determine type
- Copy/paste/duplicate signals exist but provide no built-in implementation — you must handle clipboard yourself

## Include Requirements

`ClassDB::bind_method` and `D_METHOD` require:
```cpp
#include "core/object/class_db.h"
```
The forward declaration in `object.h` is NOT sufficient.
