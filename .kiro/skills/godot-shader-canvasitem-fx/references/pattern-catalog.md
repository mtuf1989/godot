# Pattern Catalog

Use this file first when choosing or reviewing a `canvas_item` shader pattern.

## Shared Rules

- Stay inside the `canvas_item` boundary:
  - gameplay sprites
  - UI surfaces
  - full-screen overlay `ColorRect` transitions
- Treat the code snippets below as core math sketches, not full drop-in shaders.
- Default to these implementation rules unless grounded project facts force a change:
  - `shader_type canvas_item;`
  - snake_case for uniforms, locals, and helper functions
  - `group_uniforms` for inspector organization
  - `source_color` on exposed colors
  - `instance uniform` for runtime values that vary per entity
  - branch-light math with `mix()`, `step()`, `smoothstep()`, and `clamp()`
  - preserve alpha intentionally
- Treat these as integration hazards:
  - `AtlasTexture` or region-backed sprites
  - palette LUT filtering
  - outline padding at texture edges
  - multi-tap sampling cost
  - overdraw on full-screen effects

## Pick A Pattern

### Hit Flash

Use when:

- an enemy, player, or destructible needs immediate damage feedback
- one logical entity should flash without cloning the whole material per instance

Typical host:

- `Sprite2D`
- `AnimatedSprite2D`
- a parent `CanvasGroup` when several child sprites should flash together

Core uniforms:

- `instance uniform vec4 flash_color : source_color`
- `instance uniform float flash_modifier : hint_range(0.0, 1.0)`

Core fragment strategy:

```glsl
vec4 base = texture(TEXTURE, UV);
COLOR.rgb = mix(base.rgb, flash_color.rgb, flash_modifier);
COLOR.a = base.a;
```

Premultiplied alpha variant (use when the project or blend mode expects premultiplied output):

```glsl
vec4 base = texture(TEXTURE, UV);
vec3 flashed = mix(base.rgb, flash_color.rgb * base.a, flash_modifier);
COLOR = vec4(flashed, base.a);
```

Watch for:

- transparent-box bleeding when premultiplied alpha rules are ignored — if the project uses premultiplied blending, multiply the flash color RGB by `base.a` before output or the flash will fill the transparent bounding box
- all instances flashing together because `flash_modifier` was not an `instance uniform`

Validation:

- tween `flash_modifier` on one instance in a crowd
- confirm only the targeted entity flashes
- confirm transparent pixels stay transparent

### Dissolve Or Materialize

Use when:

- an entity should appear or disappear with noise-driven breakup instead of plain alpha fade
- a death, teleport, summon, or burn edge needs a bounded reusable effect

Typical host:

- `Sprite2D`
- `AnimatedSprite2D`
- a parent `CanvasGroup` when several child sprites should dissolve together as one logical entity
- other textured `CanvasItem` nodes with predictable UV mapping

Core uniforms:

- `uniform sampler2D dissolve_texture : hint_default_white`
- `instance uniform float dissolve_amount : hint_range(0.0, 1.0)`
- `uniform vec4 edge_color : source_color`
- `uniform float edge_thickness : hint_range(0.0, 0.5)`

Core fragment strategy:

```glsl
// scale noise UV by sprite pixel size to keep frequency consistent across differently sized sprites
vec2 dissolve_uv = UV * (1.0 / TEXTURE_PIXEL_SIZE) / noise_scale;
float noise = texture(dissolve_texture, dissolve_uv).r;
float visible = step(dissolve_amount, noise);
float edge = smoothstep(dissolve_amount, dissolve_amount + edge_thickness, noise);
```

Watch for:

- noise scale stretching across differently sized sprites — scale the noise UV by the sprite's pixel dimensions (via `TEXTURE_PIXEL_SIZE`) or pass a manual scale uniform so the noise frequency stays consistent regardless of sprite size
- edge glow clipping when the source texture has no transparent padding
- atlas bleed if generated or remapped UV logic ignores local sprite regions

Validation:

- animate `dissolve_amount` from `0.0` to `1.0`
- confirm the sprite fully disappears with no neighboring atlas pixels leaking in
- confirm edge thickness still reads consistently

### Outline Or Glow

Use when:

- interactables, selected units, or readable pickups need a contour
- the effect should live on the sprite rather than in a separate mesh or duplicate sprite stack

Typical host:

- `Sprite2D`
- `AnimatedSprite2D`
- small item sprites or selection targets

Core uniforms:

- `instance uniform vec4 outline_color : source_color`
- `uniform float outline_width : hint_range(0.0, 10.0)`
- `instance uniform bool enable_outline`

Core fragment strategy (4-tap cardinal sample):

```glsl
vec2 ps = TEXTURE_PIXEL_SIZE * outline_width;
float center_alpha = texture(TEXTURE, UV).a;
float neighbor_alpha = 0.0;
neighbor_alpha += texture(TEXTURE, UV + vec2(ps.x, 0.0)).a;
neighbor_alpha += texture(TEXTURE, UV + vec2(-ps.x, 0.0)).a;
neighbor_alpha += texture(TEXTURE, UV + vec2(0.0, ps.y)).a;
neighbor_alpha += texture(TEXTURE, UV + vec2(0.0, -ps.y)).a;
float edge = step(0.01, neighbor_alpha) * (1.0 - center_alpha);
```

For 8-tap (cardinal + diagonal), add the four diagonal offsets (`±ps.x, ±ps.y`) for smoother coverage at higher cost.

Soft-edge variant for anti-aliased or high-resolution art:

```glsl
// replace the hard step with smoothstep to avoid jagged outlines on soft alpha edges
float edge = smoothstep(0.0, 0.5, neighbor_alpha) * (1.0 - smoothstep(0.0, 0.1, center_alpha));
```

Watch for:

- clipped outlines when opaque pixels touch the texture border — the asset needs transparent padding or the effect needs a slightly oversized proxy host
- jagged edges on soft or antialiased art if the logic uses only hard binary alpha tests — use the `smoothstep` variant above for gradual alpha boundaries
- unnecessary GPU cost if the multi-tap path stays active when the outline is visually off — gate the expensive path behind `enable_outline`

Validation:

- test sprites with and without transparent padding
- inspect every extreme edge for flat clipping
- toggle the outline off and confirm the heavy path is bypassed

### Palette Swap

Use when:

- one sprite sheet should represent multiple factions, skins, or variants
- the project wants recolors without duplicating textures

Typical host:

- grayscale or palette-authored gameplay sprites
- `Sprite2D` or animated atlas-backed sprites, with extra care around filtering

Core uniforms:

- `uniform sampler2D palette_texture : filter_nearest`
- `instance uniform float palette_index`

Core fragment strategy:

```glsl
float shade = texture(TEXTURE, UV).r;
vec4 mapped = texture(palette_texture, vec2(shade, palette_index));
```

Watch for:

- muddy blended colors because the LUT is not using nearest filtering
- brittle exact-color equality checks instead of stable shade-to-LUT mapping
- unintended alpha changes if the original sprite alpha is not preserved

Validation:

- cycle across multiple palette rows
- confirm there is no row bleed or color interpolation
- confirm the original sprite alpha remains intact

### Gradient Background Or Panel

Use when:

- UI backgrounds, menus, sky-like backdrops, or resizable panels need procedural color transitions
- a static texture would be less flexible than a small shader

Typical host:

- `ColorRect`
- `TextureRect`
- other rectangular UI surfaces

Core uniforms:

- `instance uniform vec4 color_top : source_color`
- `instance uniform vec4 color_bottom : source_color`
- `uniform float gradient_offset : hint_range(-1.0, 1.0)`

Core fragment strategy:

```glsl
float t = clamp(UV.y + gradient_offset, 0.0, 1.0);
COLOR = mix(color_top, color_bottom, t);
```

Anti-banding dithering addition (apply after the gradient mix):

```glsl
// Bayer 2x2 dither to break 8-bit banding on subtle gradients
float bayer = mod(floor(FRAGCOORD.x) + 2.0 * floor(FRAGCOORD.y), 4.0) / 4.0;
COLOR.rgb += (bayer - 0.5) / 255.0;
```

Watch for:

- visible banding on subtle dark gradients — add the dithering snippet above or a similar noise-based offset to break 8-bit color stepping
- layout assumptions that break when the host node changes size
- using a full-screen gradient when a smaller local panel would do and overdraw would be lower

Validation:

- stretch the host across a large viewport
- test near-similar colors that provoke banding
- confirm the dithering logic improves the result instead of adding obvious patterned noise

### Screen Transition

Use when:

- scene changes, menus, or game states need a stylized full-screen wipe or reveal
- the effect should be owned by a dedicated UI overlay instead of sprinkled across gameplay nodes

Typical host:

- full-rect `ColorRect` inside a dedicated high-layer `CanvasLayer`

Core uniforms:

- `uniform sampler2D transition_mask`
- `uniform float progress : hint_range(0.0, 1.0)`
- `uniform vec4 transition_color : source_color`

Core fragment strategy (texture-mask approach):

```glsl
float mask = texture(transition_mask, mask_uv).r;
COLOR = mix(vec4(0.0), transition_color, step(mask, progress));
```

Procedural diamond variant (no external texture needed):

```glsl
// correct for aspect ratio so diamonds stay square on any screen shape
float aspect = SCREEN_PIXEL_SIZE.y / SCREEN_PIXEL_SIZE.x;
vec2 corrected_uv = vec2(UV.x * aspect, UV.y);
float diamond_size = 8.0; // number of diamonds across the screen
vec2 grid = fract(corrected_uv * diamond_size);
float mask = abs(grid.x - 0.5) + abs(grid.y - 0.5);
COLOR = mix(vec4(0.0), transition_color, step(mask, progress));
```

Watch for:

- incorrect draw order because the overlay is not isolated in its own `CanvasLayer`
- stretched diamonds, circles, or wipes when aspect ratio is ignored in generated-shape math — use `SCREEN_PIXEL_SIZE` to derive the aspect ratio and correct UV.x before shape generation
- unnecessary heavy texture sampling on already full-screen work — prefer procedural `fract()`-based shapes when the pattern is geometric and regular

Validation:

- resize to tall and ultrawide aspect ratios
- confirm the transition stays above gameplay and UI
- confirm the generated shape keeps its intended proportions

### UV Distortion Or Warp

Use when:

- a local texture needs water wobble, heat haze, magical shimmer, or simple warp motion
- the effect can stay bounded to the node instead of requiring a full post-process

Typical host:

- standalone `Sprite2D`
- `TextureRect`
- controlled UI or effect surfaces that can tolerate local UV displacement

Core uniforms:

- `uniform sampler2D noise_texture : repeat_enable`
- `uniform vec2 speed`
- `uniform float strength`

Core fragment strategy:

```glsl
vec2 noise_uv = UV + TIME * speed;
vec2 offset = (texture(noise_texture, noise_uv).rg * 2.0 - 1.0) * strength;
vec4 warped = texture(TEXTURE, UV + offset);
```

Watch for:

- edge streaks or clamp artifacts when warped UVs leave the usable range
- broken scrolling if the noise texture is not repeat-enabled
- severe atlas bleed on packed sprite sheets

Validation:

- test on a standalone texture first
- then test on any atlas-backed target separately
- document the exact atlas limit honestly if the effect is unsafe there

## Pattern Boundaries

- If the effect must read or rewrite the whole screen as a renderer-level post-process, escalate to `godot-architect`.
- If the main issue is asset padding, grayscale authoring, or LUT preparation, involve `godot-prototype-assets-2d`.
- If the work is mostly UI layout and only lightly uses a shader, let `godot-ui-core` own the layout and use this skill only for the shader slice.
