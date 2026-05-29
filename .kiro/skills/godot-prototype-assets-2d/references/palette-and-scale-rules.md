# Palette and Scale Rules

Use this file before editing or reviewing any prototype asset set.

## Shape First

- Distinguish gameplay categories by silhouette before color.
- Keep the number of colors small and semantic.
- Treat color as reinforcement, not as the only signal.
- Start from grayscale readability, then apply the palette.

## Canonical Palette

| Role | Fill | Outline | Notes |
| --- | --- | --- | --- |
| player | `#56B4E9` | `#111111` | default hero or controllable unit |
| enemy | `#D55E00` | `#111111` | danger or hostile entity |
| pickup | `#009E73` | `#111111` | beneficial collectible |
| hazard | `#E69F00` | `#111111` | use `#F0E442` only when the background is dark enough |
| neutral icon fill | `#FFFFFF` | `#111111` | use when the icon sits inside a button or badge |
| optional rare accent | `#CC79A7` | `#111111` | special item or ability only if the set needs it |
| optional ally accent | `#0072B2` | `#111111` | non-player friendly unit only if needed |

Avoid pure red and pure green as the primary prototype signals.

## Contrast Rules

- UI icons and critical gameplay symbols should target at least 3:1 contrast against adjacent colors.
- Text outside the SVG should target at least 4.5:1 contrast unless the project already defines another approved accessibility baseline.
- If the background is busy, keep the near-black outline even when the interior fill changes.

## Stroke Ladder

- 64 x 64 world sprite: `stroke-width="4"`
- 48 x 48 medium prop: `stroke-width="3"`
- 32 x 32 pickup or hazard: `stroke-width="3"` by default, `2` only when the silhouette stays bold
- 24 x 24 icon: `stroke-width="2"`
- Default joins: `stroke-linejoin="round"`
- Default caps for lines and open shapes: `stroke-linecap="round"`

## Padding and Live Area

- 64 x 64 world sprites: keep the main silhouette inside a 48 x 48 live area.
- 32 x 32 pickups: keep the main silhouette inside a 24 x 24 live area.
- 24 x 24 icons: keep forms inside a 20 x 20 live area.
- Do not let strokes sit on the outer edge of the canvas.
- Prefer integer coordinates so rasterization stays predictable.

## Optical Centering and Visual Weight

Mathematical centre and visual centre are not the same. Directional shapes look off-centre when placed at exact geometric midpoints.

- Shift directional shapes (arrows, play triangles, chevrons) 1-2 units in their pointing direction on a 24 x 24 grid. Scale proportionally for other canvas sizes.
- A play triangle at exact centre looks too far left. Nudge it 1 unit right.
- An upward arrow at exact centre looks too low. Nudge it 1 unit up.

Visual weight also varies by shape complexity. A detailed icon looks heavier than a simple one at the same physical size. Use the blur test to check consistency: squint at the asset set or mentally blur it. If one asset looks noticeably darker or lighter than its neighbours, adjust its stroke weight or interior fill area to match the set's average density.

Circles need to be slightly larger than squares to appear the same size. Triangles need to be taller than squares to match visual weight. When mixing shape types in a set, compensate by letting rounder shapes use a slightly larger portion of the live area.

## Pixel Alignment for Icons

Icons at small sizes (16-24 px) render on actual device pixels. Misalignment causes blurry edges.

| Stroke width | Place line centres at | Why |
| --- | --- | --- |
| 1 | half-pixel (e.g. 12.5) | 0.5 + 0.5 fills exactly 1 pixel |
| 2 | integer (e.g. 12) | 1 + 1 fills exactly 2 pixels |
| 3 | half-pixel (e.g. 12.5) | 1.5 + 1.5 fills exactly 3 pixels |
| 4 | integer (e.g. 12) | 2 + 2 fills exactly 4 pixels |

For the default `stroke-width="2"` on 24 x 24 icons, integer coordinates are correct. Only reach for half-pixel values if the project uses odd stroke widths.

## Pivot and Export Rules

- Default pivot convention for world assets: centered texture with `Sprite2D.centered = true`.
- Default pivot convention for UI icons: centered geometry inside the icon box.
- If a gameplay object needs a foot or muzzle offset, handle it in the scene graph instead of baking excess transparent space into the asset.
- If the project is explicitly pixel-perfect, confirm whether centered sprites and filtering rules should change before authoring the full set.
- Canonical sizes:
  - `64`
  - `48`
  - `32`
  - `24`
  - optional `16` for tiny icons only
- Keep `width`, `height`, and `viewBox` aligned to the same numbers.

## Naming and Folder Conventions

If the project has no existing convention, default to:

- root: `res://assets/proto_2d/`
- entities: `res://assets/proto_2d/entities/`
- pickups: `res://assets/proto_2d/pickups/`
- hazards: `res://assets/proto_2d/hazards/`
- UI icons: `res://assets/proto_2d/ui/icons/`

Default filenames:

- `player_<variant>.svg`
- `enemy_<role>_<variant>.svg`
- `pickup_<type>.svg`
- `hazard_<type>.svg`
- `icon_<name>.svg`

## Godot Import Defaults

- Default mode: `svg/scale = 1.0`, with SVG design size matching the intended in-game size.
- Oversample only when the project frequently zooms or rotates these textures:
  - import at `svg/scale = 2.0`
  - display at `Sprite2D.scale = Vector2(0.5, 0.5)` or an equivalent scene-level scale
- Enable mipmaps only when downscaling or camera zoom-out is common enough to justify the memory cost.
- In Godot 4, filtering is a CanvasItem or project setting, not the old per-import toggle:
  - linear filtering is acceptable for smooth prototype art
  - nearest filtering fits pixel-art projects
- Leave editor-only SVG options disabled for gameplay assets.
