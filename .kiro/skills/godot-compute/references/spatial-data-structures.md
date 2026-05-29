# Spatial Data Structures

Reference for GPU-side uniform grids, spatial hashing, sorting, and prefix sum for neighbor search in Godot 4 compute shaders.

## Table of Contents

1. [Uniform Grid Concept](#concept)
2. [Grid Construction Strategies](#construction)
3. [Sorted Grid (CSR-Style)](#sorted-grid)
4. [Atomic Fixed-Capacity Grid](#atomic-grid)
5. [GPU Sorting](#sorting)
6. [Prefix Sum (Scan)](#prefix-sum)
7. [Neighbor Iteration](#neighbor-iteration)
8. [Buffer Layout](#buffer-layout)
9. [Godot Pass Graph](#pass-graph)

---

## 1. Uniform Grid Concept <a name="concept"></a>

A uniform grid partitions space into equal-sized cells. Each particle maps to a cell based on its position. Neighbor queries inspect only the current cell and adjacent cells.

**Cell width = interaction radius.** This ensures all possible neighbors are within the immediate neighborhood:
- 2D: 3×3 = 9 cells
- 3D: 3×3×3 = 27 cells

For boids, cell width = max perception radius. For SPH, cell width = support radius `h`.

### Cell Coordinate

```glsl
ivec3 cell = ivec3(floor((pos - domain_min) / cell_size));
```

### Linear Cell Index

For a grid of dimensions `W × H × D`:

```glsl
uint cell_hash = uint(cell.x) + uint(cell.y) * grid_width + uint(cell.z) * grid_width * grid_height;
```

For unbounded domains, use a spatial hash instead:

```glsl
uint cell_hash = (uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u) % table_size;
```

Spatial hashing trades exact cell identity for bounded memory. Collisions are handled by checking actual distances during neighbor iteration.

## 2. Grid Construction Strategies <a name="construction"></a>

Two main approaches, both O(N) for bounded particles per cell:

| Strategy | Pros | Cons |
|----------|------|------|
| **Sorted grid** (sort by hash, find cell ranges) | Contiguous memory per cell, cache-friendly iteration, no per-cell capacity limit | Requires GPU sort |
| **Atomic grid** (atomicAdd into fixed-capacity cells) | Simpler, no sort needed | Requires known max occupancy, scattered writes serialize on crowded cells |

**Recommendation:** sorted grid for production systems. Atomic grid for prototyping or very low particle counts.

## 3. Sorted Grid (CSR-Style) <a name="sorted-grid"></a>

The standard GPU pipeline: `calcHash → sort → findCellStart`.

### Pass 1: Compute Cell Hash

Each thread computes its particle's cell hash and writes `(cell_hash, particle_id)` pairs:

```glsl
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= num_particles) return;

    vec3 pos = positions[idx].xyz;
    ivec3 cell = ivec3(floor((pos - domain_min) / cell_size));
    uint hash = uint(cell.x) + uint(cell.y) * grid_w + uint(cell.z) * grid_w * grid_h;

    cell_keys[idx] = hash;
    particle_ids[idx] = idx;
}
```

### Pass 2: Sort by Cell Hash

Sort `(cell_keys, particle_ids)` pairs by `cell_keys`. See [GPU Sorting](#sorting) below.

### Pass 3: Find Cell Start/End

Detect boundaries in the sorted array:

```glsl
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= num_particles) return;

    uint hash = sorted_keys[idx];
    uint prev_hash = (idx > 0) ? sorted_keys[idx - 1] : 0xFFFFFFFFu;

    if (hash != prev_hash) {
        cell_start[hash] = idx;
        if (idx > 0) cell_end[prev_hash] = idx;
    }
    if (idx == num_particles - 1) {
        cell_end[hash] = idx + 1;
    }
}
```

### Pass 4: Reorder State (Optional but Recommended)

Scatter particle state into cell-contiguous order for cache-friendly neighbor iteration:

```glsl
void main() {
    uint sorted_idx = gl_GlobalInvocationID.x;
    if (sorted_idx >= num_particles) return;

    uint original_idx = sorted_particle_ids[sorted_idx];
    sorted_positions[sorted_idx] = positions[original_idx];
    sorted_velocities[sorted_idx] = velocities[original_idx];
}
```

## 4. Atomic Fixed-Capacity Grid <a name="atomic-grid"></a>

Simpler but less scalable:

```glsl
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= num_particles) return;

    uint hash = compute_cell_hash(positions[idx].xyz);
    uint slot = atomicAdd(cell_count[hash], 1);

    if (slot < MAX_PER_CELL) {
        grid_cells[hash * MAX_PER_CELL + slot] = idx;
    }
    // else: overflow — particle ignored for neighbor queries
}
```

Requires clearing `cell_count` to zero each frame before the hash pass.

## 5. GPU Sorting <a name="sorting"></a>

### Radix Sort

The natural choice for fixed-width integer keys. Sorts by 4-bit or 8-bit increments in repeated compute passes. O(w·N) for w key bits — effectively linear for fixed-width cell keys.

Implementation requires:
- Per-digit histogram pass (count occurrences of each digit per workgroup)
- Prefix sum over histograms (global scan)
- Scatter pass (write elements to sorted positions)
- Repeat for each digit

AMD FidelityFX Parallel Sort is a production reference implementation of this strategy.

### Counting Sort (for Bounded Key Range)

When the number of cells is known and bounded, counting sort avoids the complexity of radix sort:

1. Count particles per cell (histogram)
2. Prefix sum over cell counts → cell offsets
3. Scatter particles into sorted order using offsets

This can cut kernel count relative to full radix sort. Best when the domain is finite and cell count is known.

### Bitonic Sort

Simpler to implement but O(N log²N). Acceptable for small N (<10K) or as a fallback.

## 6. Prefix Sum (Scan) <a name="prefix-sum"></a>

Prefix sum converts per-cell counts into per-cell offsets. Required for counting sort and for building cell-start arrays from histograms.

### Blelloch Scan (Work-Efficient)

Two phases within a workgroup using shared memory:
1. **Up-sweep (reduce):** build partial sums in a tree
2. **Down-sweep:** propagate prefix sums back down

For arrays larger than one workgroup, use a **hierarchical scan**: scan within workgroups, scan the per-workgroup totals, then add the block offsets back.

### In Godot

Each scan phase is a separate dispatch with `compute_list_add_barrier()` between them. For very large arrays, the hierarchical approach requires 3 dispatches:
1. Per-block scan + write block totals
2. Scan of block totals
3. Add block offsets to each element

## 7. Neighbor Iteration <a name="neighbor-iteration"></a>

With a sorted grid, neighbor iteration for particle `i` at cell `(cx, cy, cz)`:

```glsl
vec3 force = vec3(0.0);
for (int dz = -1; dz <= 1; dz++) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            uint neighbor_hash = cell_hash(cx + dx, cy + dy, cz + dz);
            uint start = cell_start[neighbor_hash];
            uint end = cell_end[neighbor_hash];

            for (uint j = start; j < end; j++) {
                vec3 r = my_pos - sorted_positions[j].xyz;
                float dist = length(r);
                if (dist < radius && dist > 0.0) {
                    // accumulate force/density/etc.
                }
            }
        }
    }
}
```

The sorted grid makes this cache-friendly: particles in the same cell are contiguous in memory.

## 8. Buffer Layout <a name="buffer-layout"></a>

A complete sorted-grid system needs these buffers:

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| `positions` | SSBO | N × vec4 | Unsorted particle positions |
| `velocities` | SSBO | N × vec4 | Unsorted particle velocities |
| `cell_keys` | SSBO | N × uint | Per-particle cell hash |
| `particle_ids` | SSBO | N × uint | Per-particle original index |
| `sorted_keys` | SSBO | N × uint | After sort |
| `sorted_ids` | SSBO | N × uint | After sort |
| `sorted_positions` | SSBO | N × vec4 | Cell-contiguous positions |
| `sorted_velocities` | SSBO | N × vec4 | Cell-contiguous velocities |
| `cell_start` | SSBO | num_cells × uint | Start index per cell |
| `cell_end` | SSBO | num_cells × uint | End index per cell |
| `indirect_dispatch` | SSBO + DISPATCH_INDIRECT | 12 bytes | Optional: GPU-written dispatch size |

## 9. Godot Pass Graph <a name="pass-graph"></a>

All passes in a single compute list with barriers between dependent stages:

```
compute_list_begin()
  ├─ dispatch: clear cell_start/cell_end
  ├─ barrier
  ├─ dispatch: compute cell hashes
  ├─ barrier
  ├─ dispatch(es): sort (radix or counting)
  ├─ barrier
  ├─ dispatch: find cell start/end
  ├─ barrier
  ├─ dispatch: reorder state into sorted buffers
  ├─ barrier
  ├─ dispatch: simulation (density, forces, integration)
  └─ end
```

When a later pass depends on counts from an earlier pass, store those counts in a `DISPATCH_INDIRECT` buffer and use `compute_list_dispatch_indirect()` to avoid CPU readback.
