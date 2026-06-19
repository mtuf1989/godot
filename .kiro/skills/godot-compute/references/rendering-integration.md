# Rendering Integration

Reference for connecting compute shader output to Godot's rendering pipeline: MultiMesh indirect, Texture2DRD, compute→particle, compute→spatial, procedural mesh, and indirect dispatch.

## Table of Contents

1. [MultiMesh Indirect Mode](#multimesh-indirect)
2. [Texture2DRD / Texture3DRD Bridge](#texture2drd)
3. [Compute → Particle Shader](#compute-to-particle)
4. [Compute → Spatial Shader](#compute-to-spatial)
5. [Procedural Mesh Generation](#procedural-mesh)
6. [Indirect Dispatch](#indirect-dispatch)

---

## 1. MultiMesh Indirect Mode <a name="multimesh-indirect"></a>

The strongest stock-engine path for GPU-driven instancing. Compute writes transforms directly into the MultiMesh GPU buffer — no CPU readback.

### Setup

```gdscript
var mm_rid: RID = RenderingServer.multimesh_create()
RenderingServer.multimesh_allocate_data(mm_rid, instance_count,
    RenderingServer.MULTIMESH_TRANSFORM_3D, true, true, true)  # colors, custom_data, use_indirect

var buffer_rid: RID = RenderingServer.multimesh_get_buffer_rd_rid(mm_rid)
var cmd_rid: RID = RenderingServer.multimesh_get_command_buffer_rd_rid(mm_rid)
```

- `buffer_rid` — the GPU buffer backing instance data (transforms, colors, custom data)
- `cmd_rid` — the indirect draw command buffer. Second uint32 is `instanceCount`

### Buffer Stride

For `TRANSFORM_3D` + colors + custom_data, each instance occupies:

```
Transform3D: 12 floats (3×4 matrix, no bottom row)
Color:        4 floats
Custom Data:  4 floats
Total:       20 floats = 80 bytes per instance
```

### Compute Writes Transforms

```glsl
layout(std430, binding = 0) buffer MultiMeshBuffer {
    vec4 data[];  // 20 floats per instance, packed as vec4s
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint base = idx * 5;  // 5 vec4s per instance (20 floats / 4)

    // Transform3D as 3 rows of vec4 (column-major basis + origin)
    data[base + 0] = vec4(basis_x, origin.x);
    data[base + 1] = vec4(basis_y, origin.y);
    data[base + 2] = vec4(basis_z, origin.z);
    data[base + 3] = vec4(color);
    data[base + 4] = vec4(custom_data);
}
```

### GPU-Written Instance Count

With `use_indirect = true`, compute can write `instanceCount` into the command buffer to control how many instances render without CPU involvement:

```glsl
layout(std430, binding = 1) buffer CommandBuffer {
    uint vertex_count;
    uint instance_count;  // GPU writes this
    uint first_vertex;
    uint first_instance;
};
```

### Visibility

Stock MultiMesh uses a single AABB for all instances — no per-instance culling. For fine culling, either split into spatial buckets (multiple MultiMeshes) or use a custom RD draw pipeline.

## 2. Texture2DRD / Texture3DRD Bridge <a name="texture2drd"></a>

Zero-copy path from compute-written textures to any material shader.

### Setup

```gdscript
var fmt := RDTextureFormat.new()
fmt.width = 256
fmt.height = 256
fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
fmt.usage_bits = (RenderingDevice.TEXTURE_USAGE_STORAGE_BIT
    | RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT)

var view := RDTextureView.new()
var tex_rid: RID = rd.texture_create(fmt, view)

var tex2d_rd := Texture2DRD.new()
tex2d_rd.texture_rd_rid = tex_rid
```

Assign `tex2d_rd` to a `ShaderMaterial` uniform. The spatial/particle shader samples it as a normal `sampler2D`.

### Critical Rules

- Texture **must** be created on the **main** RenderingDevice. `Texture2DRD` validates this — local device RIDs will fail.
- Usage bits must include both `STORAGE_BIT` (compute writes) and `SAMPLING_BIT` (shader reads).
- The DAG (4.3+) automatically inserts barriers between compute writes and raster reads on the same device.

### For 3D Textures

Same pattern with `Texture3DRD`:

```gdscript
var tex3d_rd := Texture3DRD.new()
tex3d_rd.texture_rd_rid = tex_rid_3d
```

Useful for vector fields, density volumes, SDF data.

## 3. Compute → Particle Shader <a name="compute-to-particle"></a>

### Supported Path: Texture

A `shader_type particles` process shader can sample `Texture2DRD` or `Texture3DRD` through standard sampler uniforms. This is the documented, working path.

```glsl
// In shader_type particles:
uniform sampler2D force_field;

void process() {
    vec3 field = texture(force_field, UV_from_position(TRANSFORM[3].xyz)).xyz;
    VELOCITY += field * DELTA;
}
```

### Not Supported: Direct SSBO Binding

Stock Godot does not expose a public API to bind user SSBOs into `shader_type particles`. The particle pipeline's storage buffers are engine-internal. Data ingress is limited to texture samplers and small uniforms.

### Frame Ordering Constraint

Particle update (`particles_storage->update_particles()`) runs **before** viewport drawing. `CompositorEffect` callbacks run **during** viewport rendering. Therefore:

- A CompositorEffect compute pass is **too late** for same-frame particle consumption
- For same-frame coupling, the compute dispatch must execute on the render thread **before** particle update
- If you cannot guarantee earlier execution, use **double-buffered one-frame latency**: particles sample the previous frame's texture

### When to Switch to Compute + MultiMesh

Switch when GPUParticles3D becomes an impedance mismatch:
- You need authoritative per-particle structured state from compute
- You need custom spawn/kill/compaction logic
- You need SSBO reads, not just texture samples
- You need rigid same-frame feedback between simulation stages

At that point, bypass GPUParticles3D entirely: compute writes transforms into a MultiMesh buffer, and a spatial shader on the MultiMeshInstance3D handles rendering.

### Native Vector Field Attractor

`GPUParticlesAttractorVectorField3D` is already a texture-driven path — it samples a `Texture3D` during particle simulation. Use it when the field is low-frequency, artist-authored, or lightly animated. Use a compute-generated `Texture3DRD` when the field is procedurally generated each frame.

## 4. Compute → Spatial Shader <a name="compute-to-spatial"></a>

### Texture Path (Recommended)

Compute writes to a texture via `imageStore()`. Spatial shader samples it via `sampler2D`. Bridge with `Texture2DRD`.

**Synchronization:** use `CompositorEffect` with `EFFECT_CALLBACK_TYPE_PRE_OPAQUE` to dispatch compute before the spatial shader's draw pass. The DAG inserts the necessary memory barrier automatically.

### INSTANCE_CUSTOM Path

For per-instance data on MultiMesh, compute writes 4 floats of custom data per instance into the MultiMesh buffer. The spatial shader reads `INSTANCE_CUSTOM`:

```glsl
// In shader_type spatial:
void vertex() {
    float health = INSTANCE_CUSTOM.x;
    float team_id = INSTANCE_CUSTOM.y;
    // ...
}
```

Limited to 4 floats per instance. For richer data, combine with a texture lookup indexed by instance ID.

### MultiMesh Buffer Direct Write (4.4+)

`RenderingServer.multimesh_get_buffer_rd_rid()` returns the GPU buffer RID. Compute writes transforms, colors, and custom data directly. See [MultiMesh Indirect Mode](#multimesh-indirect).

### SSBO in Spatial Shader — Not Stock

Stock `ShaderMaterial` does not expose a public API to bind user SSBOs into `shader_type spatial`. For SSBO-based vertex pulling or structured data reads, you need a **custom RD raster pipeline** (see [Procedural Mesh](#procedural-mesh)).

## 5. Procedural Mesh Generation <a name="procedural-mesh"></a>

Three paths from compute-generated geometry to rendering:

### Path A: CPU Readback → ArrayMesh (Offline/Infrequent)

```gdscript
var bytes: PackedByteArray = rd.buffer_get_data(compute_buffer)
var positions: PackedVector3Array = bytes.to_vector3_array()
var mesh := ArrayMesh.new()
var arrays := []
arrays.resize(Mesh.ARRAY_MAX)
arrays[Mesh.ARRAY_VERTEX] = positions
mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
```

Simple but slow. GPU→CPU→GPU round-trip. Use for load-time generation only.

### Path B: GPU-Resident RD Draw Pipeline (Real-Time)

Compute writes vertex/index SSBOs. Bind as vertex/index buffers for RD draw:

```gdscript
# Create vertex buffer (can also dispatch compute directly into it)
var vb: RID = rd.vertex_buffer_create(vb_size, PackedByteArray())
rd.buffer_copy(compute_output, vb, 0, 0, vb_size)

# Create index buffer
var ib: RID = rd.index_buffer_create(idx_count,
    RenderingDevice.INDEX_BUFFER_FORMAT_UINT32, PackedByteArray())
rd.buffer_copy(compute_indices, ib, 0, 0, ib_size)

# Draw
var dl: int = rd.draw_list_begin(framebuffer, ...)
rd.draw_list_bind_render_pipeline(dl, render_pipeline)
rd.draw_list_bind_vertex_buffers_format(dl, vertex_format, vertex_count, [vb], offsets)
rd.draw_list_bind_index_array(dl, ib)
rd.draw_list_draw(dl, true, 1, 0)
rd.draw_list_end()
```

For variable-size output, use **indirect draw**: compute writes vertex count into an indirect buffer, then `draw_list_draw_indirect()`.

### Path C: Vertex Pulling (Advanced)

No vertex buffers at all. Vertex shader reads from an SSBO by `VERTEX_ID`:

```glsl
layout(std430, binding = 0) buffer Vertices { vec4 positions[]; };
void vertex() {
    VERTEX = positions[VERTEX_ID].xyz;
}
```

Draw with `procedural_vertex_count` instead of bound vertex buffers. Requires a custom RD render pipeline. Godot does not yet support SSBO binding in stock spatial shaders — this is a custom pipeline pattern.

## 6. Indirect Dispatch <a name="indirect-dispatch"></a>

GPU-written dispatch parameters eliminate CPU readback for variable workloads.

### Buffer Layout

`compute_list_dispatch_indirect()` expects the standard Vulkan `VkDispatchIndirectCommand`:

```
offset + 0:  uint32 group_x
offset + 4:  uint32 group_y
offset + 8:  uint32 group_z
```

Total: **12 bytes**, 4-byte aligned.

### Buffer Creation

```gdscript
var indirect_buf: RID = rd.storage_buffer_create(12, PackedByteArray(),
    RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT)
```

### Writing Dispatch Parameters from Compute

A prior pass writes the dispatch dimensions:

```glsl
layout(std430, binding = 0) buffer IndirectCommand {
    uint group_x;
    uint group_y;
    uint group_z;
};

void main() {
    // Example: dispatch over occupied cells only
    uint occupied = atomicAdd(occupied_count, 1);
    if (gl_GlobalInvocationID.x == 0) {
        group_x = (total_occupied + WG_SIZE - 1) / WG_SIZE;
        group_y = 1;
        group_z = 1;
    }
}
```

### Dispatching Indirectly

```gdscript
rd.compute_list_add_barrier(cl)  # ensure prior pass completed
rd.compute_list_bind_compute_pipeline(cl, next_pipeline)
rd.compute_list_bind_uniform_set(cl, next_set, 0)
rd.compute_list_dispatch_indirect(cl, indirect_buf, 0)
```

### Alignment Warning

Under `std430`, isolate the indirect command into its **own SSBO binding** at offset 0. Embedding it inside a larger struct risks padding from `vec3` (16-byte alignment) or runtime arrays shifting the offset. A dedicated 12-byte buffer is safest.
