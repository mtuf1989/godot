# SVG Recipes

Use this file when creating or reviewing actual prototype SVG assets.

## Safe Subset

- Allowed by default:
  - `rect`
  - `circle`
  - `ellipse`
  - `polygon`
  - `polyline`
  - `line`
  - `path`
  - `g`
  - simple `transform`
  - inline `fill`, `stroke`, `stroke-width`, `stroke-linecap`, `stroke-linejoin`, and `opacity`
- Required:
  - `xmlns="http://www.w3.org/2000/svg"`
  - explicit `width`
  - explicit `height`
  - matching `viewBox`
- Keep coordinates integer-aligned where possible.
- Keep the background transparent. Do not add a full-canvas background rect.
- Prefer inline attributes over `<style>` blocks.
- Avoid unless explicitly justified and preflighted:
  - `<text>`
  - filters
  - masks
  - `clipPath`
  - embedded raster `<image>`
  - gradients
  - external CSS

## Base Templates

### World Sprite: 64 x 64

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <!-- Keep the silhouette inside the padded live area. -->
</svg>
```

### Pickup: 32 x 32

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <!-- Keep the silhouette inside the padded live area. -->
</svg>
```

### UI Icon: 24 x 24

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <!-- Use a 2-unit stroke and keep forms inside the 20 x 20 live area. -->
</svg>
```

## Recipe Catalog

### Player: Capsule With Direction Pip

Use for top-down characters that need a clear facing cue.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <rect x="18" y="14" width="28" height="36" rx="14"
        fill="#56B4E9" stroke="#111111" stroke-width="4"
        stroke-linejoin="round" />
  <circle cx="32" cy="14" r="4" fill="#111111" />
</svg>
```

### Player: Ship Wedge

Use for shooters or fast-moving player tokens.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <polygon points="32,10 50,52 32,44 14,52"
           fill="#56B4E9" stroke="#111111" stroke-width="4"
           stroke-linejoin="round" />
  <circle cx="32" cy="34" r="5" fill="#111111" />
</svg>
```

### Enemy: Spiky Diamond

Use when the enemy must read as dangerous from silhouette alone.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <polygon points="32,10 40,22 54,24 44,34 48,48 32,40 16,48 20,34 10,24 24,22"
           fill="#D55E00" stroke="#111111" stroke-width="4"
           stroke-linejoin="round" />
  <circle cx="26" cy="30" r="3" fill="#111111" />
  <circle cx="38" cy="30" r="3" fill="#111111" />
</svg>
```

### Enemy: Square Brute

Use for heavy or tank-like enemies.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <rect x="16" y="16" width="32" height="32" rx="6"
        fill="#D55E00" stroke="#111111" stroke-width="4" />
  <rect x="22" y="28" width="20" height="8" rx="4" fill="#111111" />
</svg>
```

### Pickup: Coin Or Core

Use for generic collectibles, currency, or energy.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <circle cx="16" cy="16" r="11"
          fill="#E69F00" stroke="#111111" stroke-width="3" />
  <circle cx="16" cy="16" r="4" fill="#111111" opacity="0.25" />
</svg>
```

### Pickup: Health Plus

Use when the pickup meaning must survive without text.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <circle cx="16" cy="16" r="12"
          fill="#009E73" stroke="#111111" stroke-width="3" />
  <rect x="14" y="8" width="4" height="16" rx="2" fill="#111111" />
  <rect x="8" y="14" width="16" height="4" rx="2" fill="#111111" />
</svg>
```

### Hazard: Spikes

Use for floor hazards or tile-edge threats.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="32" viewBox="0 0 64 32">
  <polygon points="4,28 12,8 20,28 28,8 36,28 44,8 52,28 60,8 60,28"
           fill="#E69F00" stroke="#111111" stroke-width="3"
           stroke-linejoin="round" />
</svg>
```

### Hazard: Warning Triangle

Use for generic danger markers, traps, or damage zones.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <polygon points="16,4 30,28 2,28"
           fill="#E69F00" stroke="#111111" stroke-width="3"
           stroke-linejoin="round" />
  <rect x="14" y="12" width="4" height="10" rx="2" fill="#111111" />
  <circle cx="16" cy="25" r="2" fill="#111111" />
</svg>
```

### Icon: Play

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <polygon points="9,6 19,12 9,18"
           fill="#FFFFFF" stroke="#111111" stroke-width="2"
           stroke-linejoin="round" />
</svg>
```

### Icon: Pause

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <rect x="7" y="6" width="4" height="12" rx="1"
        fill="#FFFFFF" stroke="#111111" stroke-width="2" />
  <rect x="13" y="6" width="4" height="12" rx="1"
        fill="#FFFFFF" stroke="#111111" stroke-width="2" />
</svg>
```

### Icon: Back

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <polyline points="14,6 8,12 14,18"
            fill="none" stroke="#111111" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

### Icon: Checkmark

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <polyline points="6,12 10,16 18,8"
            fill="none" stroke="#111111" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

### Icon: X Close

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <line x1="7" y1="7" x2="17" y2="17"
        stroke="#111111" stroke-width="2" stroke-linecap="round" />
  <line x1="17" y1="7" x2="7" y2="17"
        stroke="#111111" stroke-width="2" stroke-linecap="round" />
</svg>
```

### Icon: Arrow Right

Use for forward navigation or directional cues. Shift the shape 1 unit right from mathematical centre so it looks visually centred.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <line x1="5" y1="12" x2="19" y2="12"
        stroke="#111111" stroke-width="2" stroke-linecap="round" />
  <polyline points="14,7 19,12 14,17"
            fill="none" stroke="#111111" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

### Pickup: Heart

Use for health, lives, or love-themed collectibles.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <path d="M16 27 C9 21 4 17 4 12 A5 5 0 0 1 9 7 C11 7 13 8.5 16 11 C19 8.5 21 7 23 7 A5 5 0 0 1 28 12 C28 17 23 21 16 27Z"
        fill="#D55E00" stroke="#111111" stroke-width="3"
        stroke-linejoin="round" />
</svg>
```

### Pickup: Star

Use for score, rating, or special collectibles.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <polygon points="16,4 19,12 28,12 21,18 23,26 16,22 9,26 11,18 4,12 13,12"
           fill="#E69F00" stroke="#111111" stroke-width="3"
           stroke-linejoin="round" />
</svg>
```

### Pickup: Key

Use for doors, locks, or progression gates.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <circle cx="12" cy="12" r="7"
          fill="#E69F00" stroke="#111111" stroke-width="3" />
  <circle cx="12" cy="12" r="3" fill="#111111" opacity="0.25" />
  <rect x="17" y="10" width="11" height="4" rx="2"
        fill="#E69F00" stroke="#111111" stroke-width="3" />
  <rect x="24" y="14" width="4" height="4" rx="1"
        fill="#E69F00" stroke="#111111" stroke-width="2" />
</svg>
```

### Entity: Shield

Use for defence power-ups or armoured enemies.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <path d="M32 8 L52 18 V36 C52 48 42 56 32 58 C22 56 12 48 12 36 V18Z"
        fill="#0072B2" stroke="#111111" stroke-width="4"
        stroke-linejoin="round" />
  <path d="M32 20 L42 26 V36 C42 42 37 46 32 48 C27 46 22 42 22 36 V26Z"
        fill="#111111" opacity="0.15" />
</svg>
```

### Entity: Sword

Use for melee weapons or attack power-ups.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <rect x="30" y="8" width="4" height="32" rx="2"
        fill="#AAAAAA" stroke="#111111" stroke-width="4" />
  <rect x="22" y="38" width="20" height="6" rx="3"
        fill="#E69F00" stroke="#111111" stroke-width="3" />
  <rect x="28" y="44" width="8" height="12" rx="2"
        fill="#8B4513" stroke="#111111" stroke-width="3" />
</svg>
```

### Projectile: Small Circle

Use for bullets, pellets, or small thrown objects. Sized at 16 x 16 for projectiles that should be smaller than entities.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <circle cx="8" cy="8" r="5"
          fill="#F0E442" stroke="#111111" stroke-width="2" />
</svg>
```

## Path Command Quick Reference

When basic shapes are not enough, use `<path>`. Keep paths simple and prefer integer coordinates.

| Command | Meaning | Example |
| --- | --- | --- |
| `M x y` | Move to (absolute) | `M 16 4` |
| `m dx dy` | Move to (relative) | `m 0 -4` |
| `L x y` | Line to (absolute) | `L 28 12` |
| `l dx dy` | Line to (relative) | `l 4 4` |
| `H x` | Horizontal line to | `H 20` |
| `h dx` | Horizontal line (relative) | `h 8` |
| `V y` | Vertical line to | `V 20` |
| `v dy` | Vertical line (relative) | `v 8` |
| `A rx ry rot large sweep x y` | Arc to | `A 5 5 0 0 1 28 12` |
| `C x1 y1 x2 y2 x y` | Cubic bezier | `C 19 8.5 21 7 23 7` |
| `Z` | Close path | `Z` |

Arc flag combinations (given two points and a radius, four arcs are possible):

- `large=0 sweep=0` : small arc, counter-clockwise
- `large=0 sweep=1` : small arc, clockwise
- `large=1 sweep=0` : large arc, counter-clockwise
- `large=1 sweep=1` : large arc, clockwise

## Extend Without Style Drift

- Use one dominant silhouette and at most one interior cue.
- Keep player shapes rounder than enemies.
- Keep enemy shapes more angular or blocky than pickups.
- Keep pickups rounder and friendlier than hazards.
- Keep hazards pointy and high-contrast.
- Reuse the same stroke ladder, padding, and outline color from `palette-and-scale-rules.md`.
- If a shape needs complex `path` data, confirm that basic shapes cannot achieve the same read first.

## SVG Pitfalls

These are the most common mistakes when hand-writing SVGs. Understanding the mechanics prevents silent rendering bugs.

### Strokes clip at canvas edges

Strokes are centred on the path. A `stroke-width="4"` extends 2 units outward from the path on each side. If a shape edge sits at `x=0` or `x=64`, half the stroke is outside the viewBox and gets clipped. Inset shapes by at least half the stroke width from every canvas edge. This is why the padding/live-area rules exist.

### `fill="none"` on a `<g>` hides children

Setting `fill="none"` on a group element makes every child without its own explicit `fill` invisible. Set fill on individual elements or on the root `<svg>`, never on `<g>`.

### Consolidate repeated attributes on the root

When every element shares the same `stroke`, `stroke-width`, `stroke-linecap`, and `stroke-linejoin`, set them once on the `<svg>` element instead of repeating on each child. Children can still override individual attributes when needed.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"
     stroke="#111111" stroke-width="4" stroke-linejoin="round" fill="none">
  <rect x="18" y="14" width="28" height="36" rx="14" fill="#56B4E9" />
  <circle cx="32" cy="14" r="4" fill="#111111" stroke="none" />
</svg>
```

This keeps the SVG shorter and reduces copy-paste errors when adjusting shared values.

### viewBox mismatch clips content

If `width`, `height`, and `viewBox` disagree, the SVG scales or clips unexpectedly. Always keep all three aligned to the same dimensions.

## Troubleshooting

- If the import looks wrong, simplify the SVG until it uses only basic shapes and inline paint attributes.
- If strokes clip at the bounds, add more padding or reduce stroke width. Remember strokes extend half their width beyond the path on each side.
- If icons blur at small sizes, return to integer coordinates and canonical sizes before changing the recipe.
- If an asset depends on text, convert the lettering to shapes or redesign the icon.
- If a complex vector seems risky, preflight it with the same ThorVG path that Godot will rasterize against when such validation is available.
