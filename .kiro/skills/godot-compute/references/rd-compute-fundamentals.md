# RenderingDevice Compute Fundamentals

Core reference for the RenderingDevice compute lifecycle, buffer strategy, synchronization, and the ping-pong update pattern in Godot 4.

## Table of Contents

1. [Device Selection](#device-selection)
2. [Resource Lifecycle](#resource-lifecycle)
3. [Buffer Strategy](#buffer-strategy)
4. [Synchronization Model](#synchronization)
5. [Ping-Pong Double-Buffer Pattern](#ping-pong)
6. [Device Limits](#device-limits)
7. [Resource Cleanup](#cleanup)

---

## 1. Device Selection <a name="device-selection"></a>

| Device | Source | Use Case |
|--------|--------|----------|
| **Main (global)** | `RenderingServer.get_rendering_device()` | Anything that feeds rendering — MultiMesh, Texture2DRD, spatial shaders, particle shaders |
| **Local** | `RenderingServer.create_local_rendering_device()` | Independent background compute on a separate thread — offline generation, asset baking |

The main device renders to screen. Compute work recorded on it executes as part of the engine's frame lifecycle — no manual `submit()` needed. The DAG-based barrier system (4.3+) automatically inserts memory barriers between compute writes and subsequent raster reads.

A local device is a separate command stream requiring explicit `submit()` and `sync()`. Textures/buffers on a local device **cannot** be consumed by the main renderer or `Texture2DRD` — RID validation will fail.

`RenderingDevice` is not available on the Compatibility renderer or in headless mode. Compute requires **Forward+ or Mobile**.

## 2. Resource Lifecycle <a name="resource-lifecycle"></a>

```
.glsl file → RDShaderFile (import) → RDShaderSPIRV → shader RID → compute pipeline RID
                                                                  → buffers + uniform sets
                                                                  → compute list → dispatch
```

### Shader Compilation

```gdscript
var shader_file: RDShaderFile = load("res://compute/my_shader.glsl")
var spirv: RDShaderSPIRV = shader_file.get_spirv()

assert(shader_file.base_error == "", "Shader file error: " + shader_file.base_error)
assert(spirv.compile_error_compute == "", "SPIR-V error: " + spirv.compile_error_compute)

var shader: RID = rd.shader_create_from_spirv(spirv)
var pipeline: RID = rd.compute_pipeline_create(shader)
```

Use `.glsl` files, not runtime string compilation. File-backed shaders enable pipeline precompilation and caching (4.4+).

### Uniform Set Creation

Bindings must match GLSL `layout(set = X, binding = Y)` exactly. `uniform_set_create()` validates types, sizes, and bindings at creation time. Treat as **initialization-time**, not per-frame.

```gdscript
var uniform := RDUniform.new()
uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
uniform.binding = 0
uniform.add_id(my_buffer_rid)

var uniform_set: RID = rd.uniform_set_create([uniform], shader, 0)
```

### Compute List Recording

```gdscript
var cl: int = rd.compute_list_begin()
rd.compute_list_bind_compute_pipeline(cl, pipeline)
rd.compute_list_bind_uniform_set(cl, uniform_set, 0)
rd.compute_list_set_push_constant(cl, push_bytes, push_bytes.size())
rd.compute_list_dispatch(cl, group_count_x, 1, 1)
rd.compute_list_end()
```

On the main device, ending the list queues it for the current frame. On a local device, call `rd.submit()` after.

## 3. Buffer Strategy <a name="buffer-strategy"></a>

| Data Type | Resource | Reason |
|-----------|----------|--------|
| Particle positions, velocities, forces | SSBO | Large, read/write, per-element |
| Grid cell counts, offsets, sorted indices | SSBO | Large, rebuilt per-frame |
| Indirect dispatch parameters | SSBO + `DISPATCH_INDIRECT` flag | GPU-written, 12 bytes |
| Indirect draw command buffer | SSBO | GPU-written instance counts |
| Frame constants (dt, gravity, counts) | UBO or push constant | Small, read-only, CPU-written |
| Per-dispatch scalars (≤128 bytes) | Push constant | Fastest path for tiny data |

### Storage Buffers (SSBOs)

```gdscript
var buffer: RID = rd.storage_buffer_create(byte_size, initial_data)
# For indirect dispatch:
var buffer: RID = rd.storage_buffer_create(byte_size, initial_data,
    RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT)
```

### Uniform Buffers (UBOs)

Small read-only parameter blocks. Size limited by `LIMIT_MAX_UNIFORM_BUFFER_SIZE`.

### Push Constants

Tiny per-dispatch scalars. Many devices cap at **128 bytes**. Check `LIMIT_MAX_PUSH_CONSTANT_SIZE`.

```gdscript
var push := PackedByteArray()
push.resize(16)
push.encode_float(0, delta)
push.encode_u32(4, num_particles)
push.encode_float(8, cell_size)
rd.compute_list_set_push_constant(cl, push, push.size())
```

### Buffer Updates

`buffer_update()` is **forbidden** while a compute/draw list is active. Order:
1. Finish any active list
2. `rd.buffer_update(buffer, offset, size, data)`
3. Begin the next list

## 4. Synchronization Model <a name="synchronization"></a>

### Deprecated: `barrier()` and `full_barrier()`

Both are **no-ops** in 4.3+. The engine inserts barriers automatically via DAG. Do not call them.

### `compute_list_add_barrier()`

The only user-facing sync primitive for compute. Ensures prior dispatches complete writes before subsequent dispatches read:

```gdscript
var cl: int = rd.compute_list_begin()
rd.compute_list_bind_compute_pipeline(cl, hash_pipeline)
rd.compute_list_bind_uniform_set(cl, hash_set, 0)
rd.compute_list_dispatch(cl, groups_pass1, 1, 1)

rd.compute_list_add_barrier(cl)  # Pass 2 reads what Pass 1 wrote

rd.compute_list_bind_compute_pipeline(cl, cell_start_pipeline)
rd.compute_list_bind_uniform_set(cl, cell_start_set, 0)
rd.compute_list_dispatch(cl, groups_pass2, 1, 1)
rd.compute_list_end()
```

Omit the barrier between **independent** dispatches to allow overlap.

### `submit()` / `sync()` — Local Devices Only

- `submit()` ends and executes the local frame
- `sync()` blocks CPU until GPU finishes — **never on the hot path**

On the main device, the engine owns submission. Do not call these.

### Readback

- `buffer_get_data()` — **blocks GPU**. Debugging/offline only.
- `buffer_get_data_async()` (4.4+) — callback-based, still expensive for large buffers.

**Rule:** keep simulation GPU-resident. Read back only a small stats buffer if the CPU needs info.

## 5. Ping-Pong Double-Buffer Pattern <a name="ping-pong"></a>

Two state buffers, swap roles each frame. Avoids read/write hazards.

### Setup

```gdscript
var state: Array[RID] = []
var sets: Array[RID] = []
var frame_index: int = 0

func _init_ping_pong() -> void:
    state.resize(2)
    state[0] = rd.storage_buffer_create(byte_size, initial_data_a)
    state[1] = rd.storage_buffer_create(byte_size, initial_data_b)
    # Set 0: read A (binding 0), write B (binding 1)
    sets.append(_make_state_set(state[0], state[1]))
    # Set 1: read B (binding 0), write A (binding 1)
    sets.append(_make_state_set(state[1], state[0]))
```

### Per-Frame

```gdscript
func _simulate(delta: float) -> void:
    _update_params(delta)  # before compute_list_begin()
    var ping: int = frame_index & 1
    var cl: int = rd.compute_list_begin()
    rd.compute_list_bind_compute_pipeline(cl, pipeline)
    rd.compute_list_bind_uniform_set(cl, sets[ping], 0)
    rd.compute_list_dispatch(cl, group_count, 1, 1)
    rd.compute_list_end()
    frame_index += 1
```

Renderer consumes the previous frame's buffer: `state[(frame_index - 1) & 1]`. One frame of latency — acceptable for most visual systems.

## 6. Device Limits <a name="device-limits"></a>

Query at initialization:

| Limit | Typical Desktop | Typical Mobile |
|-------|-----------------|----------------|
| `LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X/Y/Z` | 65535 | 65535 |
| `LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X/Y/Z` | 1024 | 256–512 |
| `LIMIT_MAX_COMPUTE_WORKGROUP_INVOCATIONS` | 1024 | 256–512 |
| `LIMIT_MAX_PUSH_CONSTANT_SIZE` | 128–256 | 128 |
| `LIMIT_MAX_UNIFORM_BUFFER_SIZE` | 64KB+ | 16–64KB |
| `LIMIT_MAX_STORAGE_BUFFERS_PER_SHADER_STAGE` | 8–16 | 4–8 |

```gdscript
var max_wg_x: int = rd.limit_get(RenderingDevice.LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X)
var max_push: int = rd.limit_get(RenderingDevice.LIMIT_MAX_PUSH_CONSTANT_SIZE)
```

## 7. Resource Cleanup <a name="cleanup"></a>

Every RID must be freed explicitly — not garbage collected.

```gdscript
func _cleanup() -> void:
    for s in sets: rd.free_rid(s)
    for b in state: rd.free_rid(b)
    rd.free_rid(pipeline)
    rd.free_rid(shader)
```

Place in `_notification(NOTIFICATION_PREDELETE)` or a dedicated teardown. Leaking RIDs leaks GPU memory.
