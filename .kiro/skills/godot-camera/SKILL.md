---
name: godot-camera
description: |
  Implement camera rigs, module pipelines, rig switching, blending, and shake using the
  camera_system addon (CameraDirector2D/3D, VirtualCamera2D/3D, and CameraModule subclasses).
  Use when the user needs to set up a camera system, add camera follow, dead zones, lookahead,
  platform snap, group framing, zoom snap, path-follow rails, orbit cameras, 3D collision
  avoidance, screen shake, rig blending, teleport handling, or camera accessibility settings.
  Also use when someone mentions CameraDirector, VirtualCamera, FollowModule, DeadZoneModule,
  LookaheadModule, GroupFrameModule, LimitsModule, PathFollowModule, PlatformSnapModule,
  ZoomSnapModule, OrbitFollowModule, CollisionModule, LookAtModule, ShakeModule, CameraBlend,
  camera rig, camera pipeline, camera module, rig priority, blend stack, or teleport protocol.
  Even if the user just says "make the camera follow the player," "add screen shake,"
  "the camera feels wrong," "set up a boss camera," "frame two players," or "camera clips
  through walls," this skill applies.
  Do NOT use for game feel/juice feedback sequences that include camera shake via GodotFeel
  (use godot-feel), general GDScript unrelated to camera (use godot-gdscript), or scene/resource
  ownership of camera nodes (use godot-scene-resource).
---

# Godot Camera

Build camera rigs and module pipelines using the camera_system addon with correct pipeline ordering, blend configuration, and trap avoidance.

Read `references/camera-module-reference.md` first. Read `references/genre-presets-and-traps.md` before finalizing any rig setup.

## Responsibility

- Inspect the actual scene tree and existing camera setup before making changes.
- Compose module pipelines in correct category order: Focus → Composition → Constraint → Effect.
- Configure rig priorities, blend resources, and custom blend overrides for rig switching.
- Wire the teleport protocol for discontinuous player movement (respawn, portals, level transitions).
- Set accessibility properties (shake_intensity_scale, disable_all_shake) and guide settings menu integration.
- Write custom CameraModule subclasses when the built-in modules don't cover the use case.
- Validate with the strongest available method and report limits honestly.

## Use When

- The task requires setting up or modifying a camera system using the camera_system addon.
- A camera rig needs modules added, reordered, or configured.
- Rig switching, blending, or priority management is involved.
- The user reports camera jitter, flyback, clipping, or other camera misbehavior.
- Screen shake needs to be added via ShakeModule (not GodotFeel's FBCameraShake).
- A genre-specific camera preset needs implementation (platformer, top-down, fighting, orbit, rail).
- Camera accessibility settings need wiring.

## Do Not Use When

- The camera shake is part of a GodotFeel feedback sequence — route to `godot-feel`.
- The task is general GDScript unrelated to camera — route to `godot-gdscript`.
- Scene/resource ownership of camera rigs is the concern — route to `godot-scene-resource`.
- Architecture decisions about which camera approach to use are still open — route to `godot-architect`.
- The task is about save/load of camera settings — route to `godot-persistence`, then return here for implementation.

## Core Mental Model

Four building blocks, one data flow:

```
CameraDirector2D/3D          — one per viewport, evaluates priorities, runs pipelines, writes output
  └── VirtualCamera2D/3D     — a camera intent (rig), holds priority, contains module children
       ├── FocusModule        — sets state.position/transform from scratch (one per rig)
       ├── CompositionModule  — refines position (dead zone, lookahead, platform snap)
       ├── ConstraintModule   — clamps to bounds or avoids geometry (limits, zoom snap, collision)
       └── EffectModule       — adds noise on top (shake — always last)
```

Each physics frame: Director picks highest-priority enabled rig → creates fresh CameraState → passes it through modules in tree order → blends if transitioning → writes to output camera. Standby rigs cost zero.

The pipeline order is the single most important concept. Getting it wrong is the source of most camera bugs. One focus module per rig. Constraints before effects. Shake always last.

## Workflow

1. Read `references/camera-module-reference.md`, then inspect the project:
   - existing CameraDirector and output camera nodes
   - existing VirtualCamera rigs and their module children
   - the target node the camera should track
   - whether the game is 2D or 3D
   - existing input actions (for OrbitFollowModule3D gamepad support)
   In editor-connected mode, use `find_nodes` with type "CameraDirector2D", "CameraDirector3D", "VirtualCamera2D", or "VirtualCamera3D" for targeted lookups.

2. Set up the director if not present:
   - Add CameraDirector2D/3D to the scene
   - Set `output_camera_path` to point at the output Camera2D/Camera3D
   - Optionally set `default_blend` for smooth rig transitions

3. Build the rig with modules in correct pipeline order:
   - Consult `references/genre-presets-and-traps.md` for the genre preset closest to the task
   - Add exactly one focus module (Follow, GroupFrame, PathFollow, OrbitFollow)
   - Add composition modules in order (DeadZone → Lookahead → PlatformSnap)
   - Add constraint modules (ZoomSnap → Limits for 2D; Collision for 3D)
   - Add ShakeModule last if shake is needed
   - Set `target` NodePaths on modules that need them

4. Configure rig switching when multiple rigs exist:
   - Set priorities (higher wins)
   - Create CameraBlend resources for transitions
   - Use CameraBlendOverride in `custom_blends` for per-pair transitions
   - Blend precedence: rig.blend_in → director.custom_blends → director.default_blend → instant CUT

5. Wire the teleport protocol for discontinuous movement:
   ```gdscript
   player.global_position = new_spawn_point
   director.teleport()  # AFTER moving the player
   ```

6. Wire accessibility:
   ```gdscript
   director.shake_intensity_scale = user_preference  # 0.0–1.0
   director.disable_all_shake = user_preference       # bool
   ```

7. Write files to disk using native file editing. After writing `.gd` files, call `refresh_filesystem`.

8. Validate:
   - Editor-connected: `get_diagnostics` on changed scripts, verify rig structure via `find_nodes`
   - Check for pipeline validation warnings in the Output panel (the rig warns about multiple focus modules, out-of-order categories)
   - `run_scene` to verify camera behavior at runtime
   - Source-only: state the exact editor validation step still required

9. Escalate if needed:
   - Camera design decisions (which approach, how many rigs) → `godot-architect`
   - Game feel feedback sequences with camera shake → `godot-feel`
   - Scene/resource ownership of dynamically spawned rigs → `godot-scene-resource`
   - Save/load of camera preferences → `godot-persistence`
   - General GDScript outside camera → `godot-gdscript`

## Output

- Module pipeline in correct tree order with property values and rationale.
- GDScript for rig switching, teleport wiring, accessibility binding, or custom modules.
- Blend configuration (default_blend, custom_blends, per-rig blend_in) when transitions are involved.
- Validation performed and its limits.
- Exact blocker and next action if blocked.

## Quality Gates

- Exactly one focus module per rig. No duplicate focus modules.
- Modules are in correct category order: Focus → Composition → Constraint → Effect.
- ShakeModule is always the last child in the rig.
- `output_camera_path` is set on the director.
- Camera2D built-in smoothing properties are not re-enabled on the output camera.
- `director.teleport()` is called after every discontinuous player movement.
- 3D CollisionModule3D.pivot_offset matches OrbitFollowModule3D.pivot_offset.
- OrbitFollowModule3D input actions exist in the project's Input Map or are remapped.
- Accessibility properties are exposed to the player via settings UI.
- Typed GDScript throughout.

## Failure Modes

- Do not put two focus modules in one rig — the second overwrites the first.
- Do not place ShakeModule before LimitsModule — shake gets clamped near boundaries.
- Do not forget `director.teleport()` after teleporting the player — causes spring flyback.
- Do not re-enable Camera2D built-in smoothing on the output camera — causes jitter.
- Do not use mismatched `pivot_offset` between OrbitFollowModule3D and CollisionModule3D.
- Do not use LookAtModule3D with OrbitFollowModule3D — orbit handles its own rotation.
- Do not set OrbitFollowModule3D pitch limits to exactly ±90° — causes degenerate transform.
- Do not point PlatformSnapModule2D at a non-CharacterBody2D target — silently no-ops.
- Do not claim validation that was not performed.
- Do not make camera design decisions during implementation — escalate to `godot-architect`.

## References

Read only as needed:

- `references/camera-module-reference.md` — full module API: properties, defaults, categories, signals
- `references/genre-presets-and-traps.md` — copy-paste genre presets and hidden traps
- `../../foundation/Godot Nuanced Development Practices.md`
