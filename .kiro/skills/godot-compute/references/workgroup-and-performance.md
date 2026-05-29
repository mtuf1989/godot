# Workgroup Sizing and Performance

Reference for workgroup sizing, occupancy, shared memory, bank conflicts, and GPU profiling in Godot 4 compute shaders.

## Table of Contents

1. [Workgroup Sizing Strategy](#sizing)
2. [Desktop GPU Guidance](#desktop)
3. [Mobile GPU Guidance](#mobile)
4. [1D vs 2D Workgroups](#1d-vs-2d)
5. [Shared Memory](#shared-memory)
6. [Bank Conflicts](#bank-conflicts)
7. [Memory Coalescing](#coalescing)
8. [Profiling](#profiling)

---

## 1. Workgroup Sizing Strategy <a name="sizing"></a>

Workgroup size depends on hardware execution width, register pressure, and shared memory usage. **Always treat it as a device-profiled parameter** — provide at least two variants and measure.

### Dispatch Calculation

```gdscript
var group_count: int = ceili(float(num_elements) / float(WG_SIZE))
rd.compute_list_dispatch(cl, group_count, 1, 1)
```

Guard against out-of-bounds in the shader:

```glsl
layout(local_size_x = 128) in;
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= num_elements) return;
}
```

## 2. Desktop GPU Guidance <a name="desktop"></a>

| Vendor | Execution Width | Start Size | Notes |
|--------|----------------|------------|-------|
| **NVIDIA** | 32 (warp) | 128 | Multiples of 32. Range 128–256. Occupancy limited by registers + shared memory per SM. |
| **AMD** | 64 (GCN) / 32 (RDNA) | 128 | Multiples of 64 for GCN portability. Occupancy limited by VGPR + LDS. |
| **Intel Xe** | Variable subgroup | 128 | Profile 64/128/256. Occupancy depends on subgroup size + shared local memory jointly. |

**Default for particle/simulation kernels:** `local_size_x = 128`, with `64` and `256` as measured variants.

## 3. Mobile GPU Guidance <a name="mobile"></a>

| Vendor | Execution Width | Start Size | Notes |
|--------|----------------|------------|-------|
| **Arm Mali** | Variable | 64 | Divisible by 4. Avoid >64. Shared memory goes through L/S cache, not dedicated scratchpad. |
| **Qualcomm Adreno** | 8–128 | 32 or 64 | Wave size varies by GPU series. Align first dimension with subgroup width. |

**Default for mobile:** `local_size_x = 64`, with `32` as fallback. Prefer compact SoA layouts and minimal read/write counts.

## 4. 1D vs 2D Workgroups <a name="1d-vs-2d"></a>

| Kernel Type | Layout | Reason |
|-------------|--------|--------|
| Particle simulation (boids, SPH, integration) | 1D: `local_size_x = 128` | One thread per particle, no spatial locality needed |
| Image processing (blur, divergence, pressure) | 2D: `8×8` | Neighborhood locality improves cache and shared-memory tiling |
| Terrain heightmap passes | 2D: `8×8` or `16×16` | One thread per texel, 2D neighborhood access |
| Grid cell operations | 1D: `local_size_x = 128` | One thread per cell, linear iteration |

## 5. Shared Memory <a name="shared-memory"></a>

GLSL `shared` variables provide on-chip storage visible to all invocations in a workgroup.

### When It Helps — Tile-and-Reuse Pattern

1. Each thread loads one source element from SSBO to `shared`
2. `barrier()` (GLSL workgroup barrier)
3. Every thread reads from `shared` for its own interactions
4. Partial sums stay in registers
5. Each thread writes one final result to SSBO

Converts many uncached SSBO reads into one load plus many low-latency reuses.

### When It Hurts

- **Low reuse:** if each thread only reads its own data, `shared` adds overhead
- **Occupancy loss:** shared memory competes with occupancy — 32KB per workgroup may prevent co-residency
- **Arm Mali:** shared memory goes through L/S cache, not dedicated scratchpad. Only use when invocations genuinely cooperate

### SPH/Boids Tiling Example

```glsl
shared vec4 tile_pos[TILE_SIZE];
shared vec4 tile_vel[TILE_SIZE];

void main() {
    if (local_id < source_count) {
        tile_pos[local_id] = positions[source_start + local_id];
        tile_vel[local_id] = velocities[source_start + local_id];
    }
    barrier();

    vec3 force = vec3(0.0);
    for (uint s = 0; s < source_count; s++) {
        vec3 r = my_pos - tile_pos[s].xyz;
        // ... accumulate force ...
    }
}
```

Dense SPH gains more from tiling than sparse boids. Profile before committing.

## 6. Bank Conflicts <a name="bank-conflicts"></a>

Shared memory is organized into banks (32 banks × 32-bit on NVIDIA and AMD). When multiple threads access different addresses mapping to the same bank, access serializes.

**Mitigation:** use SoA in shared memory (`shared float pos_x[N]; shared float pos_y[N];`) rather than AoS (`shared Particle p[N];`).

Not directly detectable in Godot. Diagnose via vendor profilers (Nsight, RGP) or by observing disproportionate latency in shared-memory-heavy kernels.

## 7. Memory Coalescing <a name="coalescing"></a>

SSBO reads are most efficient when consecutive threads access consecutive addresses (coalesced access).

| Layout | Pattern | Coalescing |
|--------|---------|------------|
| **SoA**: `pos_x[N], pos_y[N], pos_z[N]` | Thread `i` reads `pos_x[i]` | ✅ Coalesced |
| **AoS**: `particles[N].x, .y, .z, .vx, ...` | Thread `i` reads `particles[i].x` | ⚠️ Strided if struct is large |

For large particle arrays, **SoA is generally better for compute**. For small structs (≤16 bytes), AoS can be acceptable.

## 8. Profiling <a name="profiling"></a>

### Godot Built-in

```gdscript
# GPU timestamps
rd.capture_timestamp("sim_start")
# ... dispatch ...
rd.capture_timestamp("sim_end")
# After frame:
var gpu_ms = (rd.get_captured_timestamp_gpu_time(1) - rd.get_captured_timestamp_gpu_time(0)) / 1000.0

# Debug labels (for RenderDoc)
rd.draw_command_begin_label("Boids Grid Build", Color.BLUE)
# ... dispatches ...
rd.draw_command_end_label()

# Resource names (for RenderDoc)
rd.set_resource_name(particle_buffer, "boids_state_a")
```

Launch with `--gpu-validation` to catch sync/descriptor issues before tuning.

### External Tools

| Tool | Vendor | Key Metrics |
|------|--------|-------------|
| **RenderDoc** | All | Buffer contents, dispatch inspection, resource flow |
| **Nsight Graphics** | NVIDIA | SM throughput, cache, warp occupancy, "warp can't launch" reasons |
| **Nsight Systems** | NVIDIA | CPU/GPU timeline, SM utilization, DRAM activity |
| **Radeon GPU Profiler** | AMD | Queue sync, barrier timing, ISA, wave occupancy, VGPR/SGPR |

### Discipline

1. Name every buffer, wrap every pass in timestamped debug labels
2. RenderDoc first — verify correct buffers are written/consumed
3. `--gpu-validation` to catch hazards before believing timing
4. Nsight or RGP for occupancy, barrier cost, memory saturation
5. Change one thing at a time, then re-measure
