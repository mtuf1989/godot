# Phase 4.5 Design Notes — Editor Polish & Remaining Nodes

## Status (as of 2026-05-29)

### Completed
- **4.5B (Node Library)** — All 13 new nodes implemented and registered. Total: 20 operators.
- **Float→Audio implicit conversion** — Compiler auto-promotes Float→Audio connections.
- **Categorized sub-menus** — Add Node menu grouped by category.
- **Save button** — Added to toolbar, uses `save_resource_as()` for first save, `ResourceSaver::save()` for subsequent.
- **Parameter editing** — SpinBox controls on each GraphNode for inline param editing.

### Remaining: 4.5A (Editor Polish)

---

## UI Bugs to Fix (from user feedback)

### Bug 1: "â" character in pin labels
The `→` (U+2192) arrow between input/output pin names renders as `â` due to font encoding issues. 

**Fix:** Don't concatenate input and output labels on the same row. Instead, use separate left-aligned and right-aligned labels. When a slot has both an input and output pin, show the input name left-aligned and output name right-aligned (or just show the input name on the left side and output name on the right side of the node naturally via GraphNode's slot system).

**Root cause:** In `_create_graph_node`, the code does:
```cpp
text = String(op_desc->inputs[i].name) + "  →  " + String(op_desc->outputs[i].name);
```
This should be replaced with proper left/right label separation.

### Bug 2: Output pin label alignment
The "output" text should be right-aligned (flush with the right pin dot), not left-aligned or concatenated with input text. GraphNode supports this via separate left/right child controls or using `set_text_direction`.

**Fix:** Use an HBoxContainer with a left Label (input name) and a right Label (output name, right-aligned) for slots that have both pins.

### Feature 3: Collapsible parameters (show/hide toggle)
Add a small button on the GraphNode title bar to collapse/expand the parameter SpinBoxes. When collapsed, only pin slots are visible — makes the node compact. Similar to Unreal Engine Blueprint node collapse.

**Implementation:** Add a `Button` (toggle, small "▼"/"▶" icon) to the title bar. When toggled, hide/show the parameter HBoxContainer rows. Store collapse state in `NodeDesc` (or a separate editor-only map).

### Feature 4: Comment node (optional, low priority)
A non-functional node that displays user text on the canvas. Used for documentation/organization.

**Implementation options:**
- Godot 4.6 has `GraphFrame` (successor to the deprecated `GraphComment`/`VisualShaderNodeComment`). We can use `GraphFrame` directly — it's a built-in GraphEdit feature that groups nodes with a title and description. No custom operator needed.
- Alternative: A custom "Comment" operator with no pins, just a text param displayed as a Label.

**Recommendation:** Use `GraphFrame` — it's already in Godot 4.6, supports resizing, and groups nodes visually. Just need to add a "Add Comment Frame" button to the toolbar.

---

## 4.5A: Editor Polish Features (Full List)

Priority order:

1. **Fix pin label rendering** (Bug 1 & 2 above)
2. **Collapsible parameters** (Feature 3)
3. **Undo/Redo** — Integrate with `EditorUndoRedoManager`
4. **Comment frames** — Use `GraphFrame` (Feature 4)
5. **Copy/Paste/Duplicate** — Selection-based node duplication
6. **Wire visual polish** — Type-based thickness/color already works via slot colors; add line thickness differentiation if possible
7. **Inspector integration** — Show selected node params in Godot's Inspector panel

---

## 4.5C: Sub-Graph (Patch) Support

Deferred. Requires:
1. Patch node type referencing another `.tres` Symphony resource
2. Dynamic pin display matching sub-graph's GraphInput/GraphOutput
3. Compiler flattening (inline sub-graph nodes into parent)
4. Editor UX (double-click to open, breadcrumb navigation)
5. Cross-boundary cycle detection
6. Resource dependency tracking

---

## Decisions Made This Session

| Decision | Choice |
|----------|--------|
| Float→Audio conversion | Implicit promotion (compiler fills 64-sample buffer from single float) |
| Biquad cutoff modulation | Per-micro-block coefficient recalculation (not per-sample) |
| Compressor detection | Peak-based for v1, RMS mode param reserved (needs ring buffer in arena) |
| LFO output type | Float pin (control-rate), connects to Audio inputs via implicit promotion |
| Node menu organization | Sub-menus by category |
| WavePlayer | Deferred to 4.5A (needs resource picker widget, not just SpinBox) |

---

## Technical Notes for Next Session

### Current file structure:
```
modules/symphony/
├── editor/symphony_editor_plugin.{h,cpp}  — GraphEdit editor, save, preview
├── core/                                   — Compiler, registry, arena, types
├── stream/                                 — AudioStreamSymphony, Playback
├── nodes/generators/                       — Oscillator, Constant, Noise, LFO
├── nodes/filters/                          — BiquadFilter, OnePole, DCBlocker, Saturator
├── nodes/envelopes/                        — Gain, ADSR, Compressor
├── nodes/math/                             — MathAdd, Mix, MapRange, SampleHold
├── nodes/timing/                           — Clock, TriggerDelay
└── nodes/io/                               — GraphInput, GraphOutput, TriggerInput
```

### Build command:
```bash
scons platform=macos target=editor rendering_driver=opengl3 vulkan=no -j$(sysctl -n hw.ncpu)
```

### Key APIs used:
- `GraphEdit::get_connections()` returns `Vector<Ref<GraphEdit::Connection>>`
- `GraphNode::set_slot(idx, left_enable, left_type, left_color, right_enable, right_type, right_color)`
- `EditorPlugin::add_control_to_bottom_panel()`, `make_bottom_panel_item_visible()`, `hide_bottom_panel()`
- `EditorNode::get_singleton()->save_resource_as(resource)`
- `ResourceSaver::save(resource, path)`
- `Dictionary::get_key_list()` returns `LocalVector<Variant>` (Godot 4.6)
- `AudioStreamPlayback::set_parameter(StringName, Variant)` — base class virtual we override

### Compressor RMS mode (future implementation notes):
When mode=1 (RMS), the compressor needs:
- A ring buffer of `window_size` floats (e.g., 480 floats for 10ms at 48kHz)
- Running sum of squared samples
- `sqrt(sum / window_size)` per sample for the RMS level
- The ring buffer must be pre-allocated in the arena during compilation
- The `create_fn` should check the mode param and allocate accordingly
- Arena size calculation in the compiler needs to account for this extra allocation

---

*Last updated: May 29, 2026*
