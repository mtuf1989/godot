# Boundary Design Checklist

Use this file before adding new methods, signals, properties, or worker-thread behavior.

## Native Justification

- Is there a measured or strongly justified reason this code must be native?
- Would typed GDScript or C# solve the problem with less operational risk?
- Is the workload actually a GPU or compute-shader candidate instead of a CPU-native one?
- Is the task narrow enough to keep GDScript in charge of orchestration?

## Boundary Shape

- Can the script side call once per chunk, frame, or job instead of once per element?
- Can inputs arrive as `Packed*Array`, `StringName`, enums, or `Ref<Resource>` instead of many tiny calls?
- Can outputs be returned as bulk data or coarse status rather than live node pointers?
- Are properties and methods only the ones the project actually needs, not everything the class could expose?

## Registration and Initialization

- Does each exposed class inherit from the correct Godot base type?
- Does each class have `GDCLASS` and `_bind_methods()`?
- Is the initialization level the lowest sane level for the feature?
- If the code is editor-only, is that separation explicit instead of leaking into runtime gameplay code?

## Ownership

- Does any boundary method return a raw `RefCounted*` value?
- If a `Node` pointer crosses the boundary, who owns it and how is stale-pointer use prevented?
- Are detached nodes explicitly destroyed if they never enter the tree?
- Are Godot-managed objects using Godot ownership rules instead of ad hoc smart-pointer conventions?

## Threading

- Does any worker thread touch the active scene tree, UI, or gameplay nodes directly?
- Are scene updates or signal emissions deferred back to the main thread?
- Are shared container writes synchronized?
- Is the boundary returning data for the main thread to apply instead of mutating the world in place?

## Reload and Packaging

- Will this edit change class names, `_bind_methods()`, properties, or initialization levels?
- If yes, is the workflow planning for an editor restart instead of trusting hot reload?
- On Windows, is the rebuild loop grounded in a real strategy for locked `.dll` and `.pdb` files?
- Are platform libraries and deployment paths explicit in `.gdextension`?

## Reject These Patterns

- Per-node or per-pixel GDScript loops that call into C++ thousands of times per frame.
- Native worker threads attaching nodes, freeing nodes, or updating transforms directly in the live scene.
- Raw `RefCounted*` contracts handed to GDScript.
- Hot-reload claims for structural API changes with no restart plan.
- Native code becoming the default place for ordinary gameplay logic.

## Performance Patterns (measured, Godot 4.6)

### Combine multiple calls into one "do everything" method
- Each GDScript→C++ bridge crossing costs ~1-2μs. Four separate calls (update + get_angles + advance_spawn_scales + set_transforms) cost 4-8μs of bridge overhead plus prevent data reuse.
- Combine into one method that does pack + compute + render. At 22k entities, this moved the ceiling from 15k to 22k.
- The combined method should accept the full sparse arrays + active_slots index array, do the sparse→dense pack internally, compute, and call engine APIs (e.g., RenderingServer) directly.

### Spatial grid state must persist across call boundaries
- When a spatial grid is built in `update_and_render()` and queried later in `find_in_radius()`, the grid origin (min_x, min_y), dimensions, and index arrays must be stored as member state.
- Cache the `active_slots` array so `find_in_radius` can map dense grid indices back to sparse slot indices for the caller.
- General rule: when splitting a monolithic function into build/query halves, audit ALL intermediate state that crosses the boundary.

### Dense-to-sparse index mapping
- Spatial grids operate on dense arrays (0..active_count-1). Callers expect sparse slot indices.
- Store the active_slots mapping in the C++ object. In `find_in_radius`, map `result[i] = active_slots_cache[dense_idx]` before returning.

### RenderingServer calls from C++ are cheaper than from GDScript
- `canvas_item_set_transform` from C++ avoids the GDScript→Engine marshalling layer. Measured: 0.20μs/call from C++ vs 0.32μs/call from GDScript (37% cheaper).
- For N > 10k entities, always batch RS calls in C++.

### D_METHOD macro limited to 13 parameter names
- godot-cpp's `D_METHOD()` silently fails at runtime (not compile time) if more than 13 param names are provided. Error: "definition has more arguments than the actual method."
- Fix: combine parameters into composite types (Rect2 for bounds, Dictionary for config bundles) to stay under the limit.

### Frustum culling for spatial operations
- Any per-entity operation that only matters near the camera (separation, overlap, animation LOD) should be gated by a bounds check: `pos.x >= rect.x && pos.x <= rect.x + rect.w && ...`
- Cost: 4 float comparisons per entity (essentially free). Savings: skips 80-90% of expensive work when entities are spread beyond the viewport.
- Pass the cull rect as a `Rect2` parameter (camera_pos - viewport_half - margin, viewport_size + margin*2).

### Temporal amortization (alternate-frame compute)
- Split entities into parity groups (even/odd index). Run expensive operations on alternating groups each frame.
- Effect: halves the cost of that operation with zero visual impact at 60 FPS (each entity updates at 30Hz).
- Applicable to: separation forces, ambient animation, non-critical physics, LOD transitions.
- NOT applicable to: collision detection, damage, input response, discrete state changes.

