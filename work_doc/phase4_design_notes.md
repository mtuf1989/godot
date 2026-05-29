# Phase 4 Design Note: Resource Serialization & Forward-Compatibility

## Context

This note was written during Phase 3 implementation to capture decisions that affect Phase 4 (Editor UI & Resource Serialization).

## Forward-Compatibility Requirements

When serializing Symphony graphs to `.tres`/`.res` files, the format must handle:

1. **New parameters added to existing node types** — Old files won't have the field. The loader must apply default values from the current OperatorDescriptor.

2. **Pin count changes on a node type** — If a node gains a new input pin in a later version, old files referencing that node won't have connections to the new pin. The compiler should treat unconnected optional pins as "use default value."

3. **Node type versioning** — Each OperatorDescriptor should carry a `version` integer. The resource loader checks the version stored in the file against the current registered version. If mismatched, a migration path runs (or a warning is emitted).

4. **Removed node types** — If a node type is removed in a future version, the loader should emit a diagnostic error ("Unknown node type 'X'") rather than crash. The graph loads with that node missing; downstream connections are broken.

## Serialization Format (Planned)

```
[resource]
script/source = "" 

[nodes]
node_0/type = "Oscillator"
node_0/version = 1
node_0/params/frequency = 440.0
node_0/position = Vector2(100, 200)  # editor-only, ignored by compiler

node_1/type = "Gain"
node_1/version = 1
node_1/params/gain = 0.5

[connections]
conn_0/from_node = 0
conn_0/from_pin = 0
conn_0/to_node = 1
conn_0/to_pin = 0
```

## Hot-Swap Limitation (Phase 3)

In Phase 3, graph hot-swap does NOT migrate operator state. When a new compiled graph replaces the old one, all operator state resets (oscillator phases, envelope states, filter histories restart from zero). This may cause an audible click/discontinuity.

**Why:** State migration requires the ExportState/ImportState protocol on every operator, plus a mapping between old and new operator instances. This is Phase 4 polish work — it only matters for live editor preview where the user expects seamless editing.

**Phase 4 action:** Implement `export_state()`/`import_state()` on operators that have audible state (oscillators, filters, delay lines). The hot-swap path should: (1) export state from old graph, (2) compile new graph, (3) import matching state by node ID, (4) atomic swap.

## GraphDescription Struct Stability

The `GraphDescription` / `NodeDesc` / `ConnectionDesc` structs used in Phase 3 are internal C++ types for programmatic graph construction. They are NOT the serialization format. Phase 4 will define the resource format independently and construct `GraphDescription` from the loaded data. Changes to the struct don't break saved files.
