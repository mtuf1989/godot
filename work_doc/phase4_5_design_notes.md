# Phase 4.5 Design Notes — Editor Polish & Remaining Nodes

## Context

Phase 4 (core) delivers: basic graph editing, serialization (.tres), connection validation, live preview, GDScript API (set_parameter/trigger), and state migration for hot-swap. Phase 4.5 completes the editor experience and adds the remaining v1 node library.

---

## 4.5A: Editor Polish Features

These features were scoped out of Phase 4 core to validate the basic edit→save→play loop first.

### Undo/Redo
- Integrate with Godot's `EditorUndoRedoManager`
- Every graph mutation (add node, delete node, connect, disconnect, change param) must be undoable
- Implementation: each action stores a forward/backward lambda or uses Godot's `do_method`/`undo_method` pattern

### Copy/Paste/Duplicate
- Selection-based: user selects a set of nodes, copies them
- Paste creates new nodes with new IDs, preserving internal connections within the selection
- External connections (wires going outside the selection) are dropped on paste
- Duplicate = copy + paste in place with offset

### Node Palette Enhancements
- Categorized tree: Generators, Filters, Envelopes, Math, Timing, I/O
- Search/filter by name
- Drag from palette onto canvas to create
- Right-click context menu on canvas for quick node creation

### Inspector Integration
- Selecting a node in GraphEdit shows its parameters in Godot's Inspector panel
- Parameters editable via Inspector (alternative to inline editing on the node)
- Changes in Inspector trigger graph recompile for live preview

### Wire Visual Polish
- Type-based color coding:
  - Audio = thick blue
  - Float = thin green
  - Int = thin yellow
  - Bool = thin white
  - Trigger = dashed orange
- Animated flow direction indicator (optional, low priority)

---

## 4.5B: Remaining V1 Node Library

Phase 4 ships with the 6 existing operators: Oscillator, Constant, Gain, ADSR, MathAdd, GraphOutput.

Phase 4.5 implements the remaining ~14 nodes to complete the v1 library:

### Generators
- **Noise** — White and pink noise (linear feedback shift register for white, Voss-McCartney for pink)
- **WavePlayer** — Plays an `AudioStreamWAV` resource with pitch/speed control. Needs resource reference in params.
- **LFO** — Sub-audio-rate oscillator (sine/tri/saw/square) outputting a Float pin, not Audio. Rate in Hz.

### Filters
- **Biquad Filter** — LP/HP/BP/Notch with audio-rate cutoff modulation. Robert Bristow-Johnson's cookbook formulas.
- **One-Pole Filter** — Simple exponential smoothing. Useful for parameter interpolation.
- **DC Blocker** — High-pass at ~5Hz to remove DC offset.
- **Saturator** — Soft clip (tanh) and hard clip. Drive parameter.

### Envelopes & Dynamics
- **Compressor** — RMS-based dynamic range compression. Threshold, ratio, attack, release.

### Math & Utility
- **Mix/Crossfade** — Blend two audio inputs with a float mix parameter (0=A, 1=B).
- **MapRange** — Remap float from [in_min, in_max] to [out_min, out_max] with optional clamp.
- **Sample & Hold** — Capture input value on trigger, hold until next trigger.

### Timing & Sequencing
- **Clock** — Generates periodic triggers at a given BPM. Output is a Trigger pin.
- **TriggerDelay** — Delays incoming triggers by N samples or milliseconds.

### I/O
- **GraphInput** — Exposes a named parameter to GDScript. Outputs Float or Trigger. (Note: this is implemented in Phase 4 core as part of the GDScript API, but listed here for completeness of the node library.)

---

## 4.5C: Sub-Graph (Patch) Support

Deferred from Phase 4 core. Requires:

1. **Patch node type** — A special node in the editor that references another `.tres` Symphony resource.
2. **Dynamic pin display** — The patch node shows input/output pins matching the referenced sub-graph's GraphInput/GraphOutput nodes.
3. **Compiler flattening** — At compile time, inline the sub-graph's nodes into the parent graph. Map sub-graph's GraphInput pins to the parent's incoming connections, and sub-graph's GraphOutput to the parent's outgoing connections.
4. **Editor UX** — Double-click a patch node to open the sub-graph in a new editor tab. Breadcrumb navigation.
5. **Cycle detection** — Must detect cycles that span across sub-graph boundaries (A references B which references A).
6. **Resource dependency tracking** — If a sub-graph changes, parent graphs that reference it should recompile.

---

## Priority Order

1. **4.5B (Remaining nodes)** — Unblocks real-world graph authoring
2. **4.5A (Editor polish)** — Undo/redo is highest priority within this group
3. **4.5C (Sub-graphs)** — Needed for v1 completion but can ship after the above

---

*Document created: May 29, 2026*
