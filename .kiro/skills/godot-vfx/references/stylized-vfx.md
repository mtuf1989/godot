# Stylized / Anime VFX Reference

Non-photorealistic rendering pipeline for anime-style visual effects: flipbooks, Texture2DArray, stepped animation, impact frames, speed lines, Yutapon cubes, Obake smears, framerate modulation, and dynamic color correction.

## Table of Contents

1. [Core Philosophy: Discrete Over Continuous](#philosophy)
2. [Flipbook Animation and Texture2DArray](#flipbooks)
3. [Framerate Modulation and Stepped Interpolation](#framerate)
4. [Impact Frames](#impact-frames)
5. [Radial Speed Lines](#speed-lines)
6. [Dynamic Color Correction](#color-correction)
7. [Yutapon Cubes (Stylized Debris)](#yutapon)
8. [Obake Smears (Velocity Stretching)](#smears)
9. [Multi-Stage Anime Effect Choreography](#choreography)

---

## 1. Core Philosophy: Discrete Over Continuous <a name="philosophy"></a>

Anime VFX inverts the goals of photorealistic rendering. Instead of smooth gradients, soft alpha blending, and volumetric light scattering, the pipeline prioritizes:

- **Hard-edged shapes** with sharp silhouettes readable during chaotic action
- **Stepped color bands** instead of smooth lighting falloff
- **Deliberate frame omission** instead of smooth interpolation
- **Exaggerated motion** that transcends realistic physics
- **Screen-space staging** that commands emotional response

The visual language is stark: shadows and highlights do not transition smoothly but step instantly between distinct bands. When an explosion expands, the transition from bright yellow core to dark orange rim is a hard edge, not a gradient. This mimics traditional cel painting and ensures instant readability.

### Key Principle: Artistic Control Over Physical Accuracy

Every arc, wisp, and dissipation in anime VFX is purposefully designed. Procedural generation lacks the deliberate staging and appeal of hand-crafted animation. The pipeline therefore favors:
- Hand-drawn flipbook sequences over procedural noise
- Authored timing curves over physics simulation
- Explicit shape language over emergent behavior

Procedural techniques (noise distortion, UV warping) are used as *overlays* on hand-crafted base animation, not as replacements.

---

## 2. Flipbook Animation and Texture2DArray <a name="flipbooks"></a>

### Why Flipbooks Dominate NPR

Hand-drawn flipbook sequences ensure every frame has deliberate shape language, staging, and appeal. A procedurally generated fire lacks the intentional composition of a flame drawn by a veteran animator.

### Legacy Atlas Problems

Traditional grid atlases (all frames packed into one large texture) suffer from:
- **Mipmap bleeding**: at lower mip levels, colors from adjacent frames bleed across UV boundaries, destroying crisp edges
- **VRAM bloat**: a 64-frame sequence at 1024×1024 per frame creates an enormous atlas

### Texture2DArray Solution

Texture2DArray stacks individual frames along the Z-axis of GPU memory:
- Each slice gets independent mipmaps — no edge bleeding
- Improved cache coherency
- Single resource binding for the entire sequence

### Shader Implementation

```glsl
shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled;

uniform sampler2DArray vfx_flipbook : source_color, filter_linear_mipmap;
uniform int total_frames = 64;

void fragment() {
    // INSTANCE_CUSTOM.y carries normalized age (0–1) from particle shader
    float age = INSTANCE_CUSTOM.y;
    float frame_float = age * float(total_frames - 1);
    float frame_index = floor(frame_float);

    vec4 color = texture(vfx_flipbook, vec3(UV, frame_index));

    ALBEDO = color.rgb;
    ALPHA = color.a;
    ALPHA_SCISSOR_THRESHOLD = 0.5; // hard alpha edges
}
```

### Frame Blending (Optional)

For smoother playback when the art style permits:

```glsl
float frame_index_a = floor(frame_float);
float frame_index_b = min(frame_index_a + 1.0, float(total_frames - 1));
float blend = fract(frame_float);

vec4 color_a = texture(vfx_flipbook, vec3(UV, frame_index_a));
vec4 color_b = texture(vfx_flipbook, vec3(UV, frame_index_b));
vec4 color = mix(color_a, color_b, blend);
```

Note: frame blending softens edges and may conflict with the hard-edged anime aesthetic. Use only when the art direction calls for it.

### Procedural Distortion Overlay

Prevent flipbook loops from looking repetitive by overlaying procedural UV distortion:

```glsl
uniform sampler2D distortion_noise : repeat_enable, filter_linear_mipmap;
uniform float distortion_strength : hint_range(0.0, 0.1) = 0.02;
uniform float noise_scroll_speed : hint_range(0.0, 2.0) = 0.3;

void fragment() {
    vec2 noise_sample = texture(distortion_noise, UV * 2.0 + TIME * noise_scroll_speed).rg;
    vec2 distorted_uv = UV + (noise_sample - 0.5) * distortion_strength;

    float frame_index = floor(INSTANCE_CUSTOM.y * float(total_frames - 1));
    vec4 color = texture(vfx_flipbook, vec3(distorted_uv, frame_index));
    // ...
}
```

This makes a short 12-frame flipbook feel like a continuous, evolving effect.

### VRAM Compression for Anime VFX

| Format | Algorithm | Quality | Recommendation |
|--------|-----------|---------|---------------|
| VRAM Uncompressed | Raw pixels | Perfect | UI elements only — unfeasible for large flipbook arrays |
| VRAM Compressed (Standard) | S3TC (DXT1/DXT5) | Low–Moderate | Distant backgrounds only — severe alpha artifacting |
| **VRAM Compressed (High Quality)** | **BPTC (BC7)** | **Excellent** | **Mandatory for anime VFX** — preserves hard alpha edges and crisp silhouettes |

BC7 doubles disk size vs DXT1 but drastically reduces VRAM vs uncompressed while maintaining near-perfect visual fidelity. The crispness of the alpha channel dictates the sharpness of anime VFX shapes — BC7 preserves this.

Always generate mipmaps for Texture2DArray. The ~33% VRAM increase is offset by drastically reduced memory bandwidth when effects are rendered at distance.

---

## 3. Framerate Modulation and Stepped Interpolation <a name="framerate"></a>

### The Anime Timing System

Traditional anime uses "limited animation" — drawings held for multiple frames to create specific rhythmic feel:

| Timing | Exposure | Drawings/Second (at 24fps base) | Feel |
|--------|----------|--------------------------------|------|
| On 1s | 1 frame per drawing | 24 | Hyper-fluid, chaotic energy |
| On 2s | 2 frames per drawing | 12 | Standard smooth animation |
| On 3s | 3 frames per drawing | 8 | Deliberate, weighty, "anime rhythm" |

### Implementing Stepped Animation in Godot

In a 60fps game engine, all transforms interpolate smoothly every frame. To achieve anime timing, artificially step the interpolation:

```gdscript
## Snaps a value to discrete steps matching anime frame timing.
## step_frames: 1 = on 1s (every frame), 2 = on 2s, 3 = on 3s
static func step_value(value: float, step_frames: int, fps: float = 60.0) -> float:
    var step_duration := step_frames / fps
    return floor(value / step_duration) * step_duration
```

For particle systems, this can be applied by:
1. Setting `fixed_fps` on GPUParticles3D to a low value (8, 12, or 24)
2. Disabling `interpolate` to prevent smoothing between ticks
3. The particles will visibly "pop" between positions, matching anime timing

### Hitstop (Framerate Modulation at Impact)

At the moment of impact, hold the current frame for 2–4 frames to force the viewer's brain to register the kinetic transfer:

```gdscript
## Brief time scale freeze for impact emphasis.
## Use godot-feel's FBTimeScale for production implementation.
func apply_hitstop(duration: float = 0.05) -> void:
    Engine.time_scale = 0.0
    await get_tree().create_timer(duration, true, false, true).timeout
    Engine.time_scale = 1.0
```

Note: for production hitstop, use `godot-feel`'s FBTimeScale feedback which handles unscaled timing correctly. This skill handles the visual VFX content; `godot-feel` handles the timing/juice wiring.

---

## 4. Impact Frames <a name="impact-frames"></a>

Impact frames are 1–3 frame visual interruptions that make hits feel devastating. They work by shocking the viewer's optical processing with extreme contrast.

### Structure

1. Strip the scene of its established color palette
2. Replace with stark monochrome or dichrome (black background, white silhouettes)
3. Optionally accent with a single vivid color (red for damage, blue for energy)
4. Hold for 1–3 frames, then snap back to normal rendering

### Implementation: CanvasLayer + ColorRect

```gdscript
## Impact frame controller. Attach to a CanvasLayer above the game.
extends CanvasLayer

@onready var flash_rect: ColorRect = $FlashRect
@onready var impact_material: ShaderMaterial = flash_rect.material

func trigger_impact(duration: float = 0.05, invert: bool = true) -> void:
    flash_rect.visible = true
    impact_material.set_shader_parameter("invert", invert)
    impact_material.set_shader_parameter("intensity", 1.0)
    await get_tree().create_timer(duration, true, false, true).timeout
    flash_rect.visible = false
```

### Impact Frame Shader

```glsl
shader_type canvas_item;

uniform sampler2D screen_texture : hint_screen_texture, filter_linear;
uniform bool invert = true;
uniform float intensity : hint_range(0.0, 1.0) = 1.0;
uniform vec4 accent_color : source_color = vec4(1.0, 0.0, 0.0, 1.0);
uniform float accent_threshold : hint_range(0.0, 1.0) = 0.8;

void fragment() {
    vec4 screen = texture(screen_texture, SCREEN_UV);
    float luminance = dot(screen.rgb, vec3(0.299, 0.587, 0.114));

    vec3 mono;
    if (invert) {
        mono = vec3(1.0 - luminance);
    } else {
        mono = vec3(luminance);
    }

    // Accent bright areas with color
    vec3 accented = mix(mono, accent_color.rgb, step(accent_threshold, luminance));

    COLOR.rgb = mix(screen.rgb, accented, intensity);
    COLOR.a = 1.0;
}
```

### Layering with Other Effects

Impact frames are most effective when combined with:
- Hitstop (via `godot-feel`) — freeze the frame during the flash
- Screen shake (via `godot-feel`) — physical camera displacement
- Particle burst (this skill) — sparks, debris, energy lines spawned at the impact point
- Speed lines (see below) — radial streaks converging on the impact

---

## 5. Radial Speed Lines <a name="speed-lines"></a>

Sharp streaks emanating from screen edges toward a focal point, creating the illusion of extreme speed or intense concentration.

### Screen-Space Implementation

Speed lines are calculated entirely in screen space via a post-processing shader on a full-screen ColorRect in a dedicated CanvasLayer.

```glsl
shader_type canvas_item;

uniform float line_density : hint_range(10.0, 200.0) = 80.0;
uniform float line_speed : hint_range(0.0, 10.0) = 3.0;
uniform float line_thickness : hint_range(0.0, 1.0) = 0.3;
uniform float center_mask_radius : hint_range(0.0, 0.8) = 0.3;
uniform vec4 line_color : source_color = vec4(1.0, 1.0, 1.0, 0.8);
uniform float intensity : hint_range(0.0, 1.0) = 0.0;

float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

void fragment() {
    vec2 centered = UV - vec2(0.5);
    float angle = atan(centered.y, centered.x);
    float dist = length(centered);

    // Generate lines based on angle
    float line_index = floor(angle * line_density / TAU);
    float line_random = hash(line_index);
    float line_width = line_random * line_thickness;

    float angle_fract = fract(angle * line_density / TAU);
    float line = smoothstep(0.5 - line_width, 0.5, angle_fract)
               * (1.0 - smoothstep(0.5, 0.5 + line_width, angle_fract));

    // Animate inward
    float streak = fract(dist * 2.0 - TIME * line_speed + line_random);
    line *= streak;

    // Mask center to keep focal area clear
    float mask = smoothstep(center_mask_radius, center_mask_radius + 0.2, dist);

    COLOR = vec4(line_color.rgb, line * mask * intensity * line_color.a);
}
```

Drive `intensity` from 0→1 via script to activate during dashes, ultimates, or concentration moments.

### Depth-Aware Speed Lines

For tighter integration with the 3D world, sample the depth texture to render speed lines behind the player character but in front of the distant environment:

```glsl
uniform sampler2D depth_texture : hint_depth_texture;

void fragment() {
    float depth = texture(depth_texture, SCREEN_UV).r;
    float depth_mask = step(depth_threshold, depth); // only draw on distant pixels
    // ... apply depth_mask to final alpha
}
```

---

## 6. Dynamic Color Correction <a name="color-correction"></a>

Temporarily overtake the screen's color grading during high-impact VFX moments.

### LUT-Based Color Correction

```glsl
shader_type canvas_item;

uniform sampler2D screen_texture : hint_screen_texture, filter_linear;
uniform sampler2D color_lut : filter_linear; // GradientTexture1D or 3D LUT
uniform float intensity : hint_range(0.0, 1.0) = 0.0;

void fragment() {
    vec4 screen = texture(screen_texture, SCREEN_UV);
    float luminance = dot(screen.rgb, vec3(0.299, 0.587, 0.114));

    // Remap through LUT
    vec3 graded = texture(color_lut, vec2(luminance, 0.5)).rgb;

    COLOR.rgb = mix(screen.rgb, graded, intensity);
    COLOR.a = 1.0;
}
```

### Common Color Correction Profiles

| Effect | Shadow Color | Highlight Color | Saturation |
|--------|-------------|----------------|------------|
| Dark magic | Deep purple | Harsh magenta | Desaturated |
| Fire ultimate | Dark red | Bright orange-white | Oversaturated warm |
| Ice attack | Deep blue | Cyan-white | Desaturated cool |
| Holy/light | Warm gold | Pure white | Slightly desaturated |

Drive `intensity` from 0→1 during the VFX sequence, then back to 0 when the effect ends.

---

## 7. Yutapon Cubes (Stylized Debris) <a name="yutapon"></a>

Named after animator Yutaka Nakamura. Environmental debris is simplified into perfect cuboid shapes rather than realistic fractured rock.

### Why Cubes

- Easier to animate and track spatially during complex choreography
- Hard right angles catch light in predictable, discrete flashes
- Geometric contrast against fluid organic motion of characters and energy
- Stronger kinetic impact than random noisy blobs

### Implementation

Use GPUParticles3D with `BoxMesh` as the draw pass mesh:
- `transform_align = DISABLED` (3D rotation, not billboard)
- Apply random initial rotation via ParticleProcessMaterial or custom particle shader
- Apply angular velocity for tumbling
- Use a cel-shaded spatial ShaderMaterial (see `vfx-shaders.md` cel-shading section)
- Scale range: vary between small chips and large chunks for visual hierarchy

### Material

```glsl
shader_type spatial;
render_mode cull_back;

uniform vec4 cube_color : source_color = vec4(0.6, 0.55, 0.5, 1.0);
uniform vec4 shadow_color : source_color = vec4(0.2, 0.18, 0.15, 1.0);

void fragment() {
    ALBEDO = cube_color.rgb;
}

void light() {
    float NdotL = dot(NORMAL, LIGHT);
    float cel = step(0.3, NdotL);
    DIFFUSE_LIGHT += mix(shadow_color.rgb, cube_color.rgb, cel) * LIGHT_COLOR * ATTENUATION;
}
```

---

## 8. Obake Smears (Velocity Stretching) <a name="smears"></a>

Obake smears distort geometry along the trajectory of movement to convey extreme speed without motion blur.

### Vertex Shader Implementation

```glsl
shader_type spatial;
render_mode unshaded, cull_disabled;

uniform float smear_strength : hint_range(0.0, 5.0) = 1.0;

void vertex() {
    // INSTANCE_CUSTOM.w carries velocity magnitude from particle shader
    float speed = INSTANCE_CUSTOM.w;

    // Stretch vertices along the velocity direction (assumed Y-forward for billboard)
    // Vertices with positive Y get pushed forward, negative Y get pulled back
    float stretch = VERTEX.y * speed * smear_strength;
    VERTEX.y += stretch;
}
```

### When to Use

- Fast-moving projectiles
- Dash effects
- Weapon swing arcs
- Any moment where the object should visually span the distance of its motion in a single frame

Smears maintain the hard-edged, cel-shaded look while faking the optical illusion of extreme speed.

---

## 9. Multi-Stage Anime Effect Choreography <a name="choreography"></a>

Anime VFX are orchestrated sequences of distinct visual states, not monolithic events.

### Typical Explosion Staging

| Stage | Timing | Visual | Implementation |
|-------|--------|--------|---------------|
| 1. Anticipation | -0.1s to 0s | Energy pulls inward, brief darkening | Attractor with negative strength, color correction |
| 2. Impact frame | 0s | Monochrome flash, 1–3 frames | CanvasLayer + impact shader |
| 3. Primary burst | 0s–0.2s | Bright emissive flash, expanding geometry | GPUParticles3D, `one_shot`, `explosiveness = 1.0` |
| 4. Shockwave | 0s–0.3s | Expanding distortion ring | Screen-reading shader on expanding mesh |
| 5. Secondary smoke | 0.1s–2s | Stylized flipbook smoke, hard edges | Sub-emitter (AT_END) with Texture2DArray |
| 6. Tertiary debris | 0.05s–1s | Yutapon cubes, sparks | Sub-emitter (AT_COLLISION) for sparks |
| 7. Speed lines | 0s–0.5s | Radial streaks | CanvasLayer + speed line shader |
| 8. Color correction | 0s–0.5s | Shifted palette | CanvasLayer + LUT shader |
| 9. Dissipation | 1s–3s | Fading smoke, cooling embers | Particle lifetime curves |

### Sub-Emitter Chain

```
Primary Burst (GPUParticles3D, one_shot)
├── Sub-emitter AT_END → Secondary Smoke (flipbook, long lifetime)
├── Sub-emitter AT_COLLISION → Tertiary Sparks (small, fast, gravity)
└── Script trigger → Impact Frame + Speed Lines + Color Correction
```

Sub-emitters handle particle-to-particle choreography on the GPU. Script triggers handle screen-space effects that are not particle-driven.

### Timing Coordination

For the script-driven screen-space effects, use `godot-feel`'s FeedbackPlayer to orchestrate timing:
- FBTimeScale for hitstop
- FBCameraShake for screen shake
- Custom feedbacks or Tween for impact frame, speed lines, and color correction intensity

This skill builds the visual content (particles, shaders, screen effects). `godot-feel` wires the timing and feedback sequences.
