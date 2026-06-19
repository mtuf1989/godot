---
name: godot-camera
description: |
  Implement camera rigs, module pipelines, rig switching, blending, and shake using the
  camera_system addon (CameraDirector2D/3D, VirtualCamera2D/3D, and CameraModule subclasses).
  Use when the user needs to set up a camera system, add camera follow, dead zones, lookahead,
  platform snap, group framing, zoom snap, path-follow rails, orbit cameras, 3D collision
  avoidance, screen shake, rig blending, teleport handling, camera accessibility settings,
  FPS camera (head bob, sway, recoil, FOV effects), TPS over-the-shoulder (shoulder offset,
  auto-center yaw, ADS), 2.5D/isometric cameras (fixed angle, pan, zoom, rotation steps,
  unit focus, group frame 3D, bounds, orbit constraints), camera zones (spatial triggers),
  camera hints (points of interest), confiner modules (polygon/box boundaries), or cutscene
  context push/pop.
  Also use when someone mentions CameraDirector, VirtualCamera, FollowModule, DeadZoneModule,
  LookaheadModule, GroupFrameModule, LimitsModule, PathFollowModule, PlatformSnapModule,
  ZoomSnapModule, OrbitFollowModule, CollisionModule, LookAtModule, ShakeModule, CameraBlend,
  ConfinerModule, CameraZone, CameraHint, HintReceiverModule, FPSAnchorModule, HeadBobModule,
  SwayModule, RecoilModule, FOVEffectModule, ShoulderModule, FixedAngleModule, ZoomModule3D,
  PanModule3D, RotationStepModule, UnitFocusModule, GroupFrameModule3D, BoundsModule3D,
  OrbitConstraintModule, camera rig, camera pipeline, camera module, rig priority, blend stack,
  teleport protocol, push_context, pop_context, or request_blend_override.
  Even if the user just says "make the camera follow the player," "add screen shake,"
  "the camera feels wrong," "set up a boss camera," "frame two players," "camera clips
  through walls," "add head bob," "over-the-shoulder camera," "isometric camera,"
  "tactics camera," "FPS camera rig," "confine camera to room," or "camera zone trigger,"
  this skill applies.
  Do NOT use for general GDScript unrelated to camera (use godot-gdscript), or scene/resource
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
- Screen shake needs to be added via ShakeModule.
- A genre-specific camera preset needs implementation (platformer, top-down, fighting, orbit, rail, FPS, TPS, 2.5D isometric, tactics).
- Camera accessibility settings need wiring (shake, head bob, FOV effects).
- Spatial triggers (CameraZone) or points of interest (CameraHint) need setup.
- Camera confinement to polygon/box boundaries is needed.
- Cutscene camera control via push_context/pop_context is needed.
- FPS camera with head bob, sway, recoil, or FOV effects is needed.
- TPS over-the-shoulder offset or auto-center yaw is needed.
- 2.5D/isometric camera with pan, zoom, rotation steps, or unit focus is needed.

## Do Not Use When

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
       ├── CompositionModule  — refines position/rotation (dead zone, lookahead, shoulder, hints, fixed angle)
       ├── ConstraintModule   — clamps to bounds or avoids geometry (limits, confiner, collision, bounds)
       └── EffectModule       — adds noise/FOV on top (shake, recoil, FOV effects — always last)
```

Each physics frame: Director picks highest-priority enabled rig (or context-forced rig) → creates fresh CameraState → passes it through modules in tree order → blends if transitioning → writes to output camera. Standby rigs cost zero.

Additional scene-level nodes that influence cameras without being modules:
- `CameraZone2D/3D` — Area-based spatial triggers that boost priority or enable rigs on enter/exit.
- `CameraHint2D/3D` — Points of interest that bias camera position via `HintReceiverModule`.

The pipeline order is the single most important concept. Getting it wrong is the source of most camera bugs. One focus module per rig. Constraints before effects. Shake/recoil/FOV always last.

**DeadZoneModule2D is a FOCUS module** (since v1.1). It replaces FollowModule2D — do NOT use both in the same rig.

## Workflow

1. Read `references/camera-module-reference.md`, then inspect the project:
   - existing CameraDirector and output camera nodes
   - existing VirtualCamera rigs and their module children
   - the target node the camera should track
   - whether the game is 2D, 3D, or 2.5D (isometric/tactics)
   - existing input actions (for OrbitFollowModule3D, PanModule3D, RotationStepModule3D, ZoomModule3D)
   - existing CameraZone or CameraHint nodes
   In editor-connected mode, use `find_nodes` with type "CameraDirector2D", "CameraDirector3D", "VirtualCamera2D", or "VirtualCamera3D" for targeted lookups.

2. Set up the director if not present:
   - Add CameraDirector2D/3D to the scene
   - Set `output_camera_path` to point at the output Camera2D/Camera3D
   - Optionally set `default_blend` for smooth rig transitions

3. Build the rig with modules in correct pipeline order:
   - Consult `references/genre-presets-and-traps.md` for the genre preset closest to the task
   - Add exactly one focus module:
     - 2D: Follow, DeadZone (replaces Follow), GroupFrame, PathFollow
     - 3D: Follow, OrbitFollow, FPSAnchor, Pan (tactics), GroupFrame3D
   - Add composition modules in order:
     - 2D: Lookahead → PlatformSnap → HintReceiver
     - 3D: LookAt → FixedAngle → Shoulder → HeadBob → Sway → ZoomModule3D → RotationStep → UnitFocus → HintReceiver3D
   - Add constraint modules:
     - 2D: Confiner → ZoomSnap → Limits
     - 3D: Confiner3D → Collision → Bounds → OrbitConstraint
   - Add effect modules last: ShakeModule → RecoilModule → FOVEffectModule
   - Set `target` NodePaths on modules that need them

4. Configure rig switching when multiple rigs exist:
   - Set priorities (higher wins)
   - Create CameraBlend resources for transitions
   - Use CameraBlendOverride in `custom_blends` for per-pair transitions
   - Blend precedence: rig.blend_in → director.custom_blends → director.default_blend → instant CUT
   - Use `request_blend_override(blend)` for one-shot overrides (consumed on next switch)

5. Set up spatial triggers if needed:
   - Add CameraZone2D/3D with CollisionShape child to define trigger area
   - Set `target_camera` to the rig to activate
   - Choose activation_mode: PRIORITY_BOOST, ENABLE, or CUSTOM
   - Set `tracking_group` to filter which bodies trigger the zone
   - Optionally set `blend_override` for zone-specific transitions

6. Set up camera hints if needed:
   - Add CameraHint2D/3D nodes at points of interest
   - Set `influence_radius` and `weight`
   - Add HintReceiverModule/HintReceiverModule3D to the rig (COMPOSITION)
   - Set `reference_node` on the receiver to the player

7. Wire the teleport protocol for discontinuous movement:
   ```gdscript
   player.global_position = new_spawn_point
   director.teleport()  # AFTER moving the player
   ```

8. Wire cutscene context if needed:
   ```gdscript
   director.push_context(cutscene_rig, blend)  # Force rig, bypass priority
   # ... cutscene plays ...
   director.pop_context(blend)                  # Restore normal priority
   ```

9. Wire accessibility:
   ```gdscript
   director.shake_intensity_scale = user_preference  # 0.0–1.0
   director.disable_all_shake = user_preference       # bool
   director.disable_fov_effects = user_preference     # bool
   director.head_bob_intensity_scale = user_preference # 0.0–1.0 (3D only, default OFF)
   director.disable_head_bob = user_preference         # bool (3D only, default true)
   ```

10. Write files to disk using native file editing. After writing `.gd` files, call `refresh_filesystem`.

11. Validate:
    - Editor-connected: `get_diagnostics` on changed scripts, verify rig structure via `find_nodes`
    - Check for pipeline validation warnings in the Output panel (the rig warns about multiple focus modules, out-of-order categories)
    - `run_scene` to verify camera behavior at runtime
    - Source-only: state the exact editor validation step still required

12. Escalate if needed:
    - Camera design decisions (which approach, how many rigs) → `godot-architect`
    - Scene/resource ownership of dynamically spawned rigs → `godot-scene-resource`
    - Save/load of camera preferences → `godot-persistence`
    - General GDScript outside camera → `godot-gdscript`

## Output

- Module pipeline in correct tree order with property values and rationale.
- GDScript for rig switching, teleport wiring, accessibility binding, cutscene context, spatial triggers, or custom modules.
- Blend configuration (default_blend, custom_blends, per-rig blend_in, request_blend_override) when transitions are involved.
- CameraZone/CameraHint setup when spatial triggers or POIs are needed.
- Validation performed and its limits.
- Exact blocker and next action if blocked.

## Quality Gates

- Exactly one focus module per rig. No duplicate focus modules.
- DeadZoneModule2D IS a focus module — do not pair with FollowModule2D.
- Modules are in correct category order: Focus → Composition → Constraint → Effect.
- Effect modules (ShakeModule, RecoilModule, FOVEffectModule) are always the last children in the rig.
- `output_camera_path` is set on the director.
- Camera2D built-in smoothing properties are not re-enabled on the output camera.
- `director.teleport()` is called after every discontinuous player movement.
- 3D CollisionModule3D.pivot_offset matches OrbitFollowModule3D.pivot_offset.
- OrbitFollowModule3D input actions exist in the project's Input Map or are remapped.
- 2.5D input actions (camera_zoom_in/out, camera_pan_*, camera_rotate_cw/ccw) exist when using those modules.
- Accessibility properties are exposed to the player via settings UI.
- `head_bob_intensity_scale` defaults to 0.0 and `disable_head_bob` defaults to true (opt-in only).
- CameraZone `tracking_group` matches the player's group membership.
- ConfinerModule2D boundary_shape points at a valid CollisionPolygon2D or Polygon2D.
- CameraState3D.ortho_size > 0 when using orthographic projection (ZoomModule3D ORTHOGRAPHIC mode).
- Typed GDScript throughout.

## Failure Modes

- Do not put two focus modules in one rig — the second overwrites the first.
- Do not pair DeadZoneModule2D with FollowModule2D — DeadZone IS a focus module since v1.1.
- Do not place ShakeModule/RecoilModule before constraint modules — effects get clamped near boundaries.
- Do not forget `director.teleport()` after teleporting the player — causes spring flyback.
- Do not re-enable Camera2D built-in smoothing on the output camera — causes jitter.
- Do not use mismatched `pivot_offset` between OrbitFollowModule3D and CollisionModule3D.
- Do not use LookAtModule3D with OrbitFollowModule3D — orbit handles its own rotation.
- Do not set OrbitFollowModule3D pitch limits to exactly ±90° — causes degenerate transform.
- Do not point PlatformSnapModule2D at a non-CharacterBody2D target — silently no-ops.
- Do not use HeadBobModule3D/SwayModule3D without a CharacterBody3D target — velocity reads fail.
- Do not enable head bob by default — `disable_head_bob` must be true by default per accessibility.
- Do not use RecoilModule mechanical recoil without connecting `mechanical_recoil_applied` signal to the player controller — aim desyncs.
- Do not use FOVEffectModule3D with OrbitFollowModule3D without also calling `get_ads_sensitivity_scale()` — ADS feels wrong.
- Do not use PanModule3D without checking `_is_rig_active()` — input leaks to inactive rigs (handled internally, but custom subclasses must replicate).
- Do not use ZoomModule3D PERSPECTIVE mode without either a target or correct `ground_height` — pivot derivation fails on non-flat terrain.
- Do not use RotationStepModule3D before FixedAngleModule3D — it reads pitch from the state that FixedAngle writes.
- Do not use push_context with a freed rig — warns and no-ops.
- Do not claim validation that was not performed.
- Do not make camera design decisions during implementation — escalate to `godot-architect`.

## References

Read only as needed:

- `references/camera-module-reference.md` — full module API: properties, defaults, categories, signals
- `references/genre-presets-and-traps.md` — copy-paste genre presets and hidden traps
- `../../foundation/Godot Nuanced Development Practices.md`
