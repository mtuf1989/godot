# Volumetric Fog and 3D Texture Reference

Volumetric fog integration, FogVolume nodes, fog shaders, Texture3D resources, and offline simulation data import for Godot 4.

## Table of Contents

1. [Global Volumetric Fog System](#global-fog)
2. [FogVolume Nodes](#fog-volumes)
3. [Custom Fog Shaders](#fog-shaders)
4. [Particle-Fog Integration](#particle-fog)
5. [Texture3D Resources](#texture3d)
6. [Offline Simulation Data Import](#offline-import)

---

## 1. Global Volumetric Fog System <a name="global-fog"></a>

Godot 4's volumetric fog uses a 3D froxel (frustum voxel) buffer to calculate fog density throughout the camera's viewable space. The fog actively interacts with light and shadows, producing god rays and volumetric light scattering.

### Enabling Volumetric Fog

Configure on the `WorldEnvironment` node's `Environment` resource:
- `volumetric_fog_enabled = true`
- `volumetric_fog_density`: base exponential density
- `volumetric_fog_albedo`: single-scattering color (white for neutral fog)
- `volumetric_fog_emission`: self-illumination color and energy
- `volumetric_fog_anisotropy`: phase function directionality
  - Positive values (toward 1.0): forward scattering (fog glows when looking toward lights)
  - Negative values (toward -1.0): back scattering
  - 0.0: isotropic scattering
- `volumetric_fog_length`: how far from camera the fog extends
- `volumetric_fog_detail_spread`: distribution of froxel resolution (more detail near camera vs far)

### Light Interaction

Volumetric fog interacts with:
- DirectionalLight3D: produces god rays and volumetric shadows
- OmniLight3D / SpotLight3D: localized volumetric glow (enable `light_volumetric_fog_energy` on the light)
- Shadow maps: fog respects shadow boundaries, creating volumetric shadow shafts

### Performance Considerations

- Froxel buffer resolution directly impacts quality and cost
- `volumetric_fog_detail_spread` concentrates resolution near the camera where it matters most
- Temporal reprojection smooths the result across frames but can ghost on fast camera movement
- Volumetric fog is Forward+ only — not available on the Mobile renderer

---

## 2. FogVolume Nodes <a name="fog-volumes"></a>

FogVolume nodes inject localized density modifications into the global froxel buffer. They act as additive or subtractive density stamps at specific world coordinates.

### Built-in Shapes

| Shape | Use Case |
|-------|----------|
| `BOX` | Room-filling fog, localized haze |
| `ELLIPSOID` | Organic cloud shapes, magical auras |
| `CYLINDER` | Pillars of light, chimney smoke columns |
| `WORLD` | Infinite volume (use with custom fog shader for global procedural fog) |

### Properties

- `size`: dimensions of the volume shape
- `material`: FogMaterial or custom ShaderMaterial with `shader_type fog`
- FogMaterial provides simple density, albedo, and emission controls
- Custom fog shaders provide full procedural control

### Layering

Multiple FogVolume nodes stack additively in the froxel buffer. Use this for:
- Localized dense fog patches within a lighter global atmosphere
- Colored fog zones (poison gas, magical energy)
- Subtractive volumes to carve clear areas within dense fog

---

## 3. Custom Fog Shaders <a name="fog-shaders"></a>

Custom `shader_type fog` shaders provide full control over density, albedo, and emission within a FogVolume.

### Shader Structure

```glsl
shader_type fog;

uniform float density_multiplier : hint_range(0.0, 5.0) = 1.0;
uniform sampler3D density_texture : filter_linear_mipmap;
uniform float scroll_speed : hint_range(0.0, 1.0) = 0.1;

void fog() {
    // UVW maps to the FogVolume's local space (0–1 in each axis)
    vec3 uvw = UVW + vec3(TIME * scroll_speed, 0.0, 0.0);
    float sampled_density = texture(density_texture, uvw).r;

    DENSITY = sampled_density * density_multiplier;
    ALBEDO = vec3(0.8, 0.85, 0.9); // slightly blue-tinted fog
    EMISSION = vec3(0.0);           // no self-illumination
}
```

### Built-in Variables

| Variable | Type | Access | Purpose |
|----------|------|--------|---------|
| `UVW` | vec3 | read | Normalized position within the FogVolume (0–1) |
| `WORLD_POSITION` | vec3 | read | World-space position of the current froxel |
| `OBJECT_POSITION` | vec3 | read | World-space position of the FogVolume node |
| `SIZE` | vec3 | read | Size of the FogVolume |
| `SDF` | float | read | Signed distance to the volume boundary |
| `DENSITY` | float | write | Output density at this point |
| `ALBEDO` | vec3 | write | Output scattering color |
| `EMISSION` | vec3 | write | Output self-illumination |

### Procedural Fog Patterns

**Noise-driven density:**
```glsl
void fog() {
    vec3 uvw = UVW * noise_scale + vec3(TIME * wind_speed, 0.0, 0.0);
    float noise = texture(noise_3d, uvw).r;
    DENSITY = noise * density_multiplier;
    // Fade density at volume edges using SDF
    DENSITY *= smoothstep(0.0, 0.1, -SDF);
}
```

**Height-based density gradient:**
```glsl
void fog() {
    float height_factor = 1.0 - smoothstep(0.0, 0.6, UVW.y);
    DENSITY = height_factor * density_multiplier;
}
```

---

## 4. Particle-Fog Integration <a name="particle-fog"></a>

Standard GPUParticles3D render as flat planes or meshes overlaid on the rendered image. They do not inherently possess true volume — they cannot block light rays traveling through the fog to cast volumetric shadows.

### Approach: Volumetric Stamping

To achieve true atmospheric integration, bypass traditional particle rendering and use a custom FogVolume shader that reads particle positions and stamps density into the froxel buffer.

Conceptual pipeline:
1. Particle system calculates positions (via GPUParticles3D or compute shader)
2. A custom fog shader iterates through particle position data
3. For each particle, the shader adds a spherical density contribution at the particle's 3D coordinate in the froxel buffer
4. The volumetric fog system handles lighting, scattering, and shadow interaction automatically

This unifies independent particle systems with the global atmospheric lighting model, allowing explosions and smoke plumes to authentically occlude directional light and project volumetric shadows.

### Practical Limitations

- Froxel buffer resolution limits the spatial precision of stamped density
- This technique is advanced and requires careful buffer management
- For most production VFX, visual approximation (particles + separate FogVolume for atmosphere) is sufficient
- Reserve true volumetric stamping for hero effects where atmospheric shadow interaction is critical

### Simpler Alternative: Paired FogVolume

For many effects, pairing a GPUParticles3D with a co-located FogVolume achieves convincing atmospheric integration without the complexity of volumetric stamping:
1. GPUParticles3D handles the visible particle sprites
2. A FogVolume at the same position adds localized density to the atmosphere
3. Animate the FogVolume's density via script to match the particle lifecycle
4. The fog naturally interacts with scene lighting, creating the illusion of volumetric particles

---

## 5. Texture3D Resources <a name="texture3d"></a>

Texture3D stores data continuously across width (X), height (Y), and depth (Z). Used for:
- Volumetric density fields (smoke, fire)
- 3D noise textures (procedural fog, turbulence)
- Vector fields (particle advection)

### Validity Requirements

For a Texture3D to be valid in Godot's rendering device:
- All constituent image slices must have the exact same pixel width and height
- All slices must have identical mipmap levels
- The total slice count must be consistent with the declared depth

### Import Workflow

Godot does not directly import raw 3D texture formats (OpenVDB, DDS with 3D layout) without custom metadata. The standard workflow:

1. Export from simulation software as a 2D atlas (flipbook of Z-slices)
2. Import into Godot as a standard image
3. Use Godot's import presets to convert to Texture3D by specifying horizontal and vertical slice counts
4. Alternatively, create Texture3D programmatically via GDScript by providing an array of Image slices

### Compression Rules

| Data Type | Compression | Rationale |
|-----------|-------------|-----------|
| Visual density (smoke shape) | VRAM Compressed OK | Visual artifacts are acceptable |
| Vector field (velocity RGB) | **Uncompressed only** | S3TC/BPTC quantizes RGB channels, causing catastrophic velocity errors |
| SDF data (distance values) | **Uncompressed only** | Compression artifacts corrupt distance calculations |
| 3D noise (procedural) | VRAM Compressed OK | Visual use tolerates compression |

For vector fields and SDF data, use 16-bit or 32-bit float per channel formats.

---

## 6. Offline Simulation Data Import <a name="offline-import"></a>

Photorealistic smoke, fire, and liquid simulations are calculated offline (Houdini, Blender, EmberGen) and baked into texture formats for real-time playback.

### Houdini → Godot Pipeline

1. **Simulate** in Houdini using Pyro, FLIP, or custom solvers
2. **Export** using Labs VolumeTexture Export node:
   - Discretize the 3D volume into Z-slices
   - Pack slices into a 2D atlas (e.g., 128³ volume → 12×12 grid of 128×128 images → 1536×1536 atlas)
   - Channel pack vector data into RGB
   - Export with Z-up axis configuration
3. **Import** into Godot:
   - Import the 2D atlas as an image
   - Convert to Texture3D via import settings (specify slice counts)
   - Apply axis conversion (Houdini Z-up → Godot Y-up)
   - For vector data: disable VRAM compression, use float format
4. **Sample** in shader:
   - Use `sampler3D` uniform
   - Map particle world-space coordinates to UVW texture coordinates
   - Extract density, velocity, or temperature from the appropriate channels

### Blender → Godot Pipeline

Blender's volume simulation can be exported similarly:
1. Render volume data to image sequences (density, velocity channels)
2. Pack into atlas format using compositing nodes or external tools
3. Follow the same Godot import workflow

### Channel Packing Conventions

| Channel | Common Data |
|---------|------------|
| R | Velocity X / Density |
| G | Velocity Y / Temperature |
| B | Velocity Z / Fuel |
| A | Density / Mask |

### Value Inversion for SDF

When working with Signed Distance Fields from external tools, the pipeline may require inverting values to match Godot's convention (negative = inside geometry, positive = outside). Verify the sign convention of your source tool before import.

### Coordinate System Conversion

| Tool | Up Axis | Forward Axis |
|------|---------|-------------|
| Godot | Y | -Z |
| Houdini | Y (default) or Z | Configurable |
| Blender | Z | -Y |

Apply the appropriate matrix transformation during export or in the sampling shader to align velocity vectors correctly.
