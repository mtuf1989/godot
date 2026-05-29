# Mass Entity Optimization (GDScript-Only)

> Measured on Godot 4.6, Apple M-series. All numbers empirical.

## When to Read This

Before implementing any system with **100+ entities** doing per-frame work: enemies, projectiles, particles, gems, status effects, flocking, spawned VFX.

## The Core Insight

Godot's per-Node engine overhead (~12-14μs/node/frame) is the dominant cost at scale — not your script logic. The engine traverses ALL nodes every frame regardless of whether they have callbacks. This is invisible to the script profiler.

At 60 FPS with 4ms of script budget: `(16.6 - 4) / 12μs ≈ 1050 nodes max.`

Everything below follows from this single fact.

## The Optimization Ladder

Apply in order. Stop when your entity count fits comfortably within the ceiling.

| Entity Count | Architecture | 60 FPS Ceiling | Key Technique |
|---|---|---|---|
| < 100 | Node-based, individual `_process()` | ~1,000 | Nothing needed |
| 100–1,000 | Manager pattern, Node pool, toggle visibility | ~1,000 | Centralized loop |
| 1,000–4,000 | Node-free PackedArrays + RS canvas items | ~4,000 | Eliminate Nodes |
| 4,000–6,000 | + Flat PackedInt32Array spatial grid | ~6,000 | Eliminate Dictionary |
| 6,000–8,000 | + Temporal amortization (parity=4) | ~8,000 | Skip work per frame |
| 8,000+ | GDExtension C++ required | ~30,000 | Eliminate interpreter |

## Measured Per-Entity Costs

```
Node existence in tree:          ~12-14μs  (irreducible without removing Node)
_draw() per node:                ~60μs     (use RS canvas items instead)
Physics body (Area2D active):    ~7μs      (disable or remove)
GDScript→Engine bridge call:     ~0.25μs   (RS transform, physics query)
Dictionary.has() + access:       ~0.1-0.2μs
PackedArray element access:      ~0.02μs
GDScript loop iteration:         ~0.03μs   (interpreter per-step)
```

## Technique 1: Node-Free Entity Architecture

**Why:** Removing Nodes eliminates the ~12μs/entity overhead — the single largest cost. Entities become indices into parallel PackedArrays. RenderingServer canvas items handle visuals.

**Structure:**
- State lives in parallel `Packed*Array` members (positions, speeds, healths)
- Active tracking via `_active_slots` (dense list) + `_free_slots` (stack for O(1) alloc) + `_is_active` (per-slot flag)
- Visuals: one RS canvas item RID per slot, created at pool init, toggled with `canvas_item_set_visible`
- Alloc = pop from free stack. Kill = hide RID + swap-and-pop from active list + push to free stack.

**Critical detail — stale reference detection:** Add a `_generations` array. Increment on kill. External references store `(slot, generation)` and validate before access.

## Technique 2: Eliminate Trig with Direction Vectors

**Why:** `atan2()` + `cos()` + `sin()` per entity is 3 unnecessary calls. The direction vector you already computed for movement IS the rotation.

```gdscript
# dir_x IS cos(angle), dir_y IS sin(angle)
var dir_x: float = to_target.x / dist
var dir_y: float = to_target.y / dist

# Build Transform2D directly — no trig needed
var cos_a: float = dir_x * scale
var sin_a: float = dir_y * scale
rs.canvas_item_set_transform(rid, Transform2D(
    Vector2(cos_a, sin_a), Vector2(-sin_a, cos_a), pos))
```

This also means you should merge movement and RS transform into ONE loop — computing direction twice (once for movement, once for rotation) is pure waste.

## Technique 3: Flat Spatial Grid (not Dictionary)

**Why:** `Dictionary.has()` costs ~0.1-0.2μs per lookup. A flat `PackedInt32Array` with prefix-sum offsets costs ~0.02μs. At 5000+ entities with 9 cell checks per entity, this is the difference between 4k and 6k ceiling.

**Approach:** One pass to count per cell → prefix sum for offsets → one pass to place indices. Query by direct index arithmetic. No hash table, no allocation per frame.

**Key rule:** Pre-allocate all work arrays as member variables. Use `.resize()` (no-op if size unchanged) instead of creating new arrays each frame.

## Technique 4: Frustum Culling

**Why:** Any per-entity operation that only matters visually (separation, overlap push, AI decisions) can be skipped for off-screen entities. The check is 4 float comparisons — essentially free.

At 30k enemies with ~3k on screen, this skips 90% of expensive work.

**Applicable to:** separation, overlap, AI detail, VFX, animation.
**NOT applicable to:** movement (entities must keep moving off-screen), damage, state changes.

## Technique 5: Temporal Amortization (Parity)

**Why:** Separation forces produce smooth/continuous results. Running them at 15Hz per entity (parity=4 at 60 FPS) is visually imperceptible but halves+ the cost.

Split entities into groups by `index % parity`. Process one group per frame.

**Works for:** separation, magnet smoothing, AI decisions, LOD transitions.
**NOT for:** collision detection, damage, input response, discrete state changes.

## What NOT to Do

| Mistake | Why it's bad | Fix |
|---|---|---|
| Dictionary grid for spatial queries | 5-10x slower than flat array at scale | PackedInt32Array + prefix-sum offsets |
| `atan2()` + `cos()` + `sin()` per entity | 3 redundant trig calls | Use direction vector directly |
| Separate movement and RS transform loops | Recomputes direction | Merge into one loop |
| `Array.append()` in hot loop | Realloc + copy | Pre-size with `.resize()` |
| New PackedArray per frame | Allocation + GC pressure | Member var + `.resize()` |
| `node.position = x` for 1000+ entities | 12-14μs/node overhead | RS `canvas_item_set_transform` |
| `queue_redraw()` per entity per frame | Full draw dispatch | Transform-only updates |
| MAX_NEIGHBORS=8+ in separation | Inner loop dominates | 4 is visually sufficient |
| Parity=2 with expensive separation | Still too many per frame | Parity=4 for GDScript |

## Decision: GDScript vs GDExtension

Choose GDScript-only when:
- Entity count stays under 5,000 in worst case
- Build simplicity matters (no native toolchain)
- The game ships on platforms where compiling C++ is impractical

Choose GDExtension when:
- Entity count exceeds 8,000
- The hot loop is pure math (steering, physics, spatial queries)
- You need the iteration loop itself in native code (not just individual calls)

The boundary is NOT "complex math" — it's **iteration count × per-iteration interpreter overhead**. A complex formula run 100 times is fine in GDScript. A simple formula run 10,000 times hits the interpreter wall.

## The Profiling Discipline

1. Add `Time.get_ticks_usec()` instrumentation with labeled columns
2. Identify which column is largest
3. Eliminate or reduce that column specifically
4. Re-measure — a new column is now dominant
5. Repeat

Never batch optimizations. Never optimize speculatively. The profiler's inclusive time lies about GDExtension-heavy functions (it includes per-Node engine overhead). Manual timing is ground truth.
