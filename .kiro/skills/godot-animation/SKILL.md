---
name: godot-animation
description: |
  Implement and integrate animation systems in Godot 4.x — AnimationPlayer, AnimationTree,
  AnimationMixer, state machines, blend trees, BlendSpace1D/2D, AnimatedSprite2D, SpriteFrames,
  Skeleton3D, BoneAttachment3D, inverse kinematics, animation retargeting, root motion,
  and procedural animation on game entities (verlet chains, spring-damper systems, 2D IK,
  creature locomotion, rope/cloth/soft-body simulation).
  Use when the user needs to add animation to characters or objects, set up AnimationPlayer
  or AnimationTree, build state machines for animation, create blend trees, animate sprites,
  implement attack combos or cancel windows, wire animation-driven hitboxes, set up skeletal
  animation or IK, retarget animations between rigs, extract root motion, build procedural
  animation systems (tentacles, tails, ropes, cloth, creature locomotion, verlet chains,
  spring bones), or integrate animation with gameplay logic.
  Also use when someone mentions AnimationPlayer, AnimationTree, AnimationMixer,
  StateMachinePlayback, BlendSpace1D, BlendSpace2D, AnimatedSprite2D, SpriteFrames,
  Skeleton3D, SkeletonIK3D, BoneAttachment3D, RetargetModifier3D, root_motion_track,
  animation_finished, frame_changed, OneShot node, blend tree, state machine transition,
  animation library, method call track, animation callback, procedural animation, verlet,
  FABRIK, CCD, spring-damper, creature locomotion, Rain World style, soft body, ragdoll,
  or asks to "animate this," "add walk cycle," "set up attack combo," "blend animations,"
  "add IK," "retarget animation," "procedural tentacle," "rope physics," "cloth simulation."
  Even if the user just says "make the character animate" or "the attack needs a hitbox
  tied to the animation" or "add a tail that follows the body" in a Godot project,
  this skill applies.
  Do NOT use for Tween-based VFX (use godot-vfx-2d),
  shader-driven visual effects (use godot-shader-canvasitem-fx),
  camera transitions (use godot-camera), UI screen transitions (use godot-common-ui),
  dialogue text animation (use godot-dialogue), or AI state machines (use godot-limboai).
---

# Godot Animation

Implement animation systems — from sprite frames to skeletal rigs to procedural creature motion — with correct API usage, clean state machine architecture, and gameplay integration.

Read `references/animation-core.md` for any AnimationPlayer or AnimationTree work. Read `references/animation-2d.md` for sprite animation or 2D procedural animation. Read `references/animation-3d.md` for skeletal animation, IK, or retargeting. Read `references/examples-and-validation.md` before finalizing.

## Responsibility

- Configure AnimationPlayer: create animations, set up tracks (value, method, bezier, audio), manage animation libraries, wire playback control.
- Build AnimationTree graphs: state machines with StateMachinePlayback, blend trees with BlendSpace1D/2D, OneShot nodes, additive layers, filter masks.
- Set up AnimatedSprite2D with SpriteFrames for frame-based 2D animation.
- Implement animation-driven gameplay: method tracks for hitbox activation, cancel windows, combo queuing, animation events.
- Write AnimationController mediator scripts that translate game state into animation parameters — gameplay logic never calls `play()` directly.
- Configure 3D skeletal animation: Skeleton3D, BoneAttachment3D, retargeting with BoneMap/SkeletonProfile, root motion extraction.
- Set up inverse kinematics: SkeletonIK3D, procedural foot placement, look-at constraints.
- Build 2D procedural animation: verlet chains, spring-damper systems, 2D FABRIK/CCD, creature locomotion, rope/cloth/soft-body simulation.
- Instrument animation systems with GLog: `GLog.transition()` for state machine changes, `GLog.info()` for animation-driven gameplay events, `GLog.debug()` with sampling for blend parameters.

## Use When

- Adding animation to a character, object, or environment element.
- Setting up AnimationPlayer, AnimationTree, or AnimatedSprite2D.
- Building animation state machines or blend trees.
- Implementing attack combos, cancel windows, or animation queuing.
- Wiring animation-driven hitboxes or gameplay events via method tracks.
- Setting up skeletal animation, IK, retargeting, or root motion.
- Building procedural animation (verlet, springs, IK, creature locomotion, rope, cloth).
- Integrating animation with game state (AnimationController pattern).
- Debugging animation timing, blending, or state machine issues.

## Do Not Use When

- Tween-based property animation for VFX (one-shot particles, trails, flashes) → `godot-vfx-2d`.
- Shader-driven visual effects on existing nodes (dissolve, outline, palette swap) → `godot-shader-canvasitem-fx`.
- Camera blend transitions or camera rig setup → `godot-camera`.
- UI screen transitions or menu animation → `godot-common-ui`.
- Dialogue text typing animation → `godot-dialogue`.
- AI behavior state machines (LimboHSM, behavior trees) → `godot-limboai`.
- Procedural animation hits GDScript performance ceiling (thousands of verlet points) → `godot-gdextension-cpp`.
- GPU compute needed for massive procedural entity counts → `godot-compute`.
- General GDScript unrelated to animation → `godot-gdscript`.

## Core Mental Model

```
Game Logic (input, AI, physics)
       │
       ▼
┌─────────────────────┐
│  AnimationController │  ← mediator script: translates game state → animation params
│  (one per entity)    │
└──────┬──────────────┘
       │  sets parameters / calls travel()
       ▼
┌─────────────────────┐
│   AnimationTree      │  ← graph: state machines, blend trees, filters, root motion
│   (AnimationMixer)   │
└──────┬──────────────┘
       │  blends & applies tracks
       ▼
┌─────────────────────┐
│  AnimationPlayer     │  ← clip library: holds Animation resources
│  (animation source)  │
└──────┬──────────────┘
       │  drives properties, calls methods, plays audio
       ▼
   Scene Nodes (Sprite2D, Skeleton3D, hitboxes, VFX spawners)
```

AnimationPlayer owns clips. AnimationTree owns blending logic. AnimationController owns the translation from gameplay to animation. Gameplay code talks to the controller, never directly to AnimationPlayer or AnimationTree.

For procedural animation (verlet, springs, IK), the controller drives control points and the procedural system generates motion — no pre-authored clips involved.

## Domain Router

Identify the animation domain, then read the appropriate reference:

| Task Domain | Reference File | Key Topics |
|---|---|---|
| AnimationPlayer, AnimationTree, state machines, blend trees, playback control | `references/animation-core.md` | Track types, libraries, state machine playback, BlendSpace, OneShot, filters, root motion |
| Sprite animation, 2D procedural animation | `references/animation-2d.md` | AnimatedSprite2D, SpriteFrames, verlet chains, springs, 2D IK, creature locomotion, rope/cloth |
| Skeletal animation, retargeting, IK, 3D procedural | `references/animation-3d.md` | Skeleton3D, BoneAttachment3D, BoneMap, SkeletonIK3D, ragdoll, procedural bone modification |

Read `animation-core.md` for most tasks — it covers the shared AnimationPlayer/AnimationTree systems used by both 2D and 3D. Add the dimension-specific reference only when the task involves dimension-specific features.

## Workflow

1. **Inspect** — read the target scene, existing animation nodes, project.godot autoloads, and any existing AnimationController scripts. Identify whether the task is 2D or 3D, clip-based or procedural.

2. **Read references** — read `animation-core.md` for AnimationPlayer/AnimationTree work. Add `animation-2d.md` or `animation-3d.md` based on the domain router above.

3. **Confirm boundary** — verify the task belongs to this skill. Tween VFX → `godot-vfx-2d`. Shader effects → `godot-shader-canvasitem-fx`. If unclear, check the Do Not Use When list.

4. **Choose architecture** — decide the animation approach:
   - Simple sprite animation → AnimatedSprite2D + SpriteFrames.
   - Single-clip playback → AnimationPlayer alone.
   - Multiple states with transitions → AnimationTree + state machine.
   - Blending by gameplay parameter → AnimationTree + BlendSpace.
   - Gameplay-driven animation events → AnimationPlayer method tracks + AnimationController.
   - Physics-responsive continuous motion → procedural animation (verlet, springs, IK).

5. **Implement** — build the animation system following the reference patterns. Always create an AnimationController mediator when using AnimationTree. Use `callback_mode_process = PHYSICS` when animation affects physics state.

6. **Wire gameplay integration** — connect animation to gameplay via method tracks (hitboxes, VFX spawns, sound cues), cancel windows, combo queuing, or root motion extraction. Follow the patterns in `animation-core.md` § Integration Patterns.

7. **Add GLog instrumentation** — log animation state transitions and gameplay events:
   ```gdscript
   # State machine transitions
   GLog.transition("Player", from_state, to_state, "animation_controller")
   # Animation-driven gameplay events
   GLog.info("hitbox_activated", "animation", {"attack": attack_name, "frame": current_frame})
   # High-frequency blend parameters (sampled)
   GLog.debug("blend_position", "animation", {"value": blend_pos}, null, GLog.SAMPLE_60)
   ```

8. **Write files** — save all scripts and scenes. Call `refresh_filesystem` after writing `.gd` files.

9. **Validate** — run `get_diagnostics` on changed scripts. Confirm animation plays correctly if a test scene exists. Check that GLog calls use correct signatures (event_name, cat, data).

10. **Escalate if needed:**
    - Animation design decisions still open → `godot-architect`.
    - Scene/resource ownership of animation nodes → `godot-scene-resource`.
    - GDScript performance ceiling for procedural animation → `godot-gdextension-cpp`.
    - GPU compute for massive entity counts → `godot-compute`.
    - General GDScript outside animation → `godot-gdscript`.

## Testing Animation Systems

Use `godot-gdunit4` patterns for animation tests. Key approaches:

- **State assertions**: `scene_runner` + `simulate_frames(N)` to advance animation, then assert state via `get_property("current_state")` or `invoke("get_animation_state")`.
- **Signal testing**: `monitor_signals()` + `assert_signal().is_emitted("animation_finished")` for completion events.
- **Cancel window testing**: `simulate_frames()` to reach the cancel window frame, then `simulate_action_pressed()` to test interruption.
- **Combo testing**: chain `simulate_action_pressed()` calls with `simulate_frames()` gaps to test input buffering.
- **Spy verification**: `spy()` on scene to verify AnimationController calls: `verify(spy_scene, 1).play_attack()`.
- **Speed up**: `set_time_factor(9.0)` for tests with long animation sequences.

Delegate test implementation details to `godot-gdunit4`. This skill defines what to test; that skill defines how.

## Quality Gates

- AnimationController mediator exists for any entity using AnimationTree — gameplay code never calls `play()` or `travel()` directly.
- `callback_mode_process` is PHYSICS when animation drives physics-affecting state (hitboxes, movement, collision shapes).
- Method tracks use `callback_mode_method = IMMEDIATE` when frame-precise timing matters (combat).
- AnimationTree state machine transitions have explicit conditions or are script-driven via `travel()` — no orphan auto-advance loops.
- BlendSpace blend points are well-distributed — no degenerate triangulation in BlendSpace2D.
- Animation resources are not shared mutably between instances — use `resource_local_to_scene` or `make_unique()` when runtime modification is needed.
- `GLog.transition()` is called for every animation state machine change.
- `GLog.info()` is called for every animation-driven gameplay event (hitbox activation, combo transition, cancel window).
- Procedural animation simulation runs in `_physics_process`; rendering interpolates in `_process`.
- Typed GDScript throughout.

## Failure Modes

- Do not call `AnimatedSprite2D.play()` every frame in `_process` — it restarts the animation. Guard with `if animation != current_animation`.
- Do not let gameplay code call `AnimationPlayer.play()` or `StateMachinePlayback.travel()` directly — use an AnimationController mediator to prevent animation spaghetti.
- Do not mix AnimatedSprite2D frame ownership with AnimationPlayer on the same node — one system owns frame selection, the other owns everything else.
- Do not use `AnimationPlayer.queue()` for combat combos — it fails with looping animations and lacks dynamic blending. Use AnimationTree state machines or manual state management.
- Do not forget `callback_mode_process = PHYSICS` when animation affects physics state — idle-mode animations desync from physics frames, causing ghost hits.
- Do not share Animation resources between instances without `resource_local_to_scene` — runtime mutations affect all users.
- Do not force IK solvers to honor invalid contacts (too far, too steep, wrong side) — invalidate, release, or project the target instead.
- Do not run procedural animation simulation in `_process` — use `_physics_process` for deterministic results, interpolate visuals in `_process`.
- Do not skip GLog instrumentation — animation transitions and gameplay events are part of the narrative log. A combat system without animation logging is incomplete.
- Do not make animation architecture decisions during implementation — escalate to `godot-architect`.

## References

Read only as needed:

- `references/animation-core.md` — AnimationPlayer, AnimationMixer, AnimationTree, state machines, blend trees, track types, playback control, root motion, integration patterns
- `references/animation-2d.md` — AnimatedSprite2D, SpriteFrames, spritesheet patterns, 2D procedural animation (verlet, springs, IK, creature locomotion, rope, cloth, soft bodies)
- `references/animation-3d.md` — Skeleton3D, BoneAttachment3D, retargeting, inverse kinematics, ragdoll, procedural bone modification
- `references/examples-and-validation.md` — worked examples and validation checklists
- `../../foundation/Godot Nuanced Development Practices.md`
