---
name: godot-feel
description: |
  Add game juice and tactile feedback to Godot 4 gameplay using the GodotFeel addon —
  FeedbackPlayer sequences, shaker wiring, timing control, and intensity-driven effects.
  Use when the user wants to make gameplay feel better, add screen shake, hit pause,
  scale pops, damage flash, pickup effects, button juice, knockback feel, or any
  layered visual/audio feedback sequence. Also trigger when the user mentions
  FeedbackPlayer, FeedbackBus, FBCameraShake, FBScale2D, FBFlash, FBTimeScale,
  FBSpring, BeatPlayer, shaker channels, game juice, game feel, or "make it juicy."
  Even if the user just says "this hit doesn't feel impactful" or "the button press
  feels flat" or "add some punch to this," this skill applies.
---

# Godot Feel

Compose layered feedback sequences using GodotFeel to make gameplay interactions feel responsive and satisfying.

Read `references/feedback-catalog.md` first. Read `references/traps-and-internals.md` when debugging issues or creating custom feedbacks. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the actual scene and script surfaces the feedback will attach to before editing.
- Compose FeedbackPlayer sequences with the right feedbacks, timing, and easing for the interaction.
- Wire shaker nodes and channels when shake feedbacks are involved.
- Set timing controls (cooldowns, delays, repeats, unscaled mode) appropriate to the gameplay context.
- Use intensity and range attenuation to make effects proportional and distance-aware.
- Use native file editing for scripts, then MCP tools for scene assembly when editor-connected.
- Validate with the strongest available method and report limits honestly.

## Use When

- The task is adding juice, feel, or feedback to an existing gameplay interaction.
- The user wants screen shake, hit pause, scale pops, flashes, sound triggers, spring motion, or particle bursts tied to game events.
- A FeedbackPlayer needs to be composed, configured, or debugged.
- The user wants rhythm-driven effects via BeatPlayer.

## Do Not Use When

- The feedback design decision (what should feel juicy and why) is still undecided — route to `godot-architect`.
- The task is general GDScript implementation unrelated to GodotFeel — route to `godot-gdscript`.
- The task is character/entity animation (AnimationPlayer, AnimationTree, sprite animation, procedural animation, attack combos) — route to `godot-animation`.
- The main issue is scene/resource ownership of FeedbackPlayer nodes — route to `godot-scene-resource`.
- The task is a visual shader effect (dissolve, outline, palette swap) — route to `godot-shader-canvasitem-fx`.
- Scope is still broad or architecture is unapproved.

## Core Mental Model

GodotFeel has three layers:

```
FeedbackPlayer (Node)       — the trigger, lives in the scene tree
    └── Feedback (Resource)     — one effect (scale, shake, flash, sound, etc.)
        └── FeedbackTiming      — when/how often/how fast
```

One FeedbackPlayer per game event (hit, collect, press), not per effect. Stack multiple feedbacks in one player, call `play()`. All feedbacks fire simultaneously unless separated by Pause or Holding Pause.

## Workflow

1. Read `references/feedback-catalog.md`, then inspect the project surfaces:
   - the scene and node the feedback attaches to
   - existing FeedbackPlayer nodes and shaker setup (use `find_nodes` with type "FeedbackPlayer" or "CameraShaker" for targeted lookups)
   - the script and signal/callback that should trigger `play()`
   - whether Camera2D/3D already has a CameraShaker child
   - existing channels in use
   In editor-connected mode, use `search_symbols`, `get_definition`, or `get_hover_info` to resolve GodotFeel API questions.

2. Compose the feedback sequence:
   - pick feedbacks from the catalog that match the interaction
   - layer them: transform + visual + audio + time effects together
   - use Holding Pause between phases when sequencing matters
   - use Initial Delay for slight staggers within a phase
   - set easing (`TRANS_BACK` + `EASE_OUT` is the workhorse for pops)

3. Wire shakers when shake feedbacks are used:
   - CameraShaker as child of Camera2D/3D
   - PositionShaker / ScaleShaker as child of the target node
   - match channel strings between feedback and shaker (exact, case-sensitive StringName)
   - one shake feedback can trigger multiple shakers on the same channel
   - use `FeedbackBus.debug = true` to verify channel connections and catch typos

4. Set timing controls appropriate to context:
   - cooldowns on frequently-triggered feedbacks (rapid-fire hits, button mashing)
   - `UNSCALED` timescale mode on any feedback that must run during a hit pause
   - `Chance` for randomized variation
   - repeats for pulsing or flickering effects

5. Use intensity for proportional effects:
   - pass damage amount, speed, or force as intensity to `play()`
   - use range attenuation with `FeedbackRange` for distance-based falloff

6. Trigger from GDScript:
   ```gdscript
   @onready var juice: FeedbackPlayer = $FeedbackPlayer
   
   func _on_event():
       juice.play()        # full intensity
       juice.play(0.5)     # half intensity
       juice.play(1.0, global_position)  # with range attenuation
   ```

7. In editor-connected mode, call `refresh_filesystem` after each changed `.gd` file.

8. Run the strongest available validation:
   - editor-connected: `get_diagnostics` on changed scripts, verify FeedbackPlayer and shaker node setup via `scene://current/tree` or `find_nodes`
   - `editor_screenshot` after `run_scene` to visually verify feedback effects (shake, flash, scale pops) in the running game
   - source-only: state the exact editor validation step still required

9. Escalate if needed:
   - feel design uncertainty → `godot-architect`
   - FeedbackPlayer scene ownership issues → `godot-scene-resource`
   - general GDScript outside GodotFeel → `godot-gdscript`
   - visual shader effects → `godot-shader-canvasitem-fx`

## Composition Defaults

Unless the task states otherwise:

- one FeedbackPlayer per game event
- easing vocabulary — pick the combo that matches the intended feel:
  - `TRANS_BACK + EASE_OUT` — punch, anticipation, overshoot (workhorse for pops and knockback)
  - `TRANS_ELASTIC + EASE_OUT` — springy, rubbery, magical (pickup bounces, UI wobble)
  - `TRANS_BOUNCE + EASE_OUT` — hard settle, impact (landing, heavy drops)
  - `TRANS_QUAD + EASE_IN_OUT` — smooth, neutral (position slides, fades)
  - `TRANS_CUBIC + EASE_OUT` — snappy deceleration (menu transitions, subtle slides)
- duration 0.05–0.15s for punchy effects, 0.2–0.4s for smooth transitions
- camera shake intensity 2–5 for light impacts, 8–15 for heavy
- hit pause duration 0.02–0.05s via FBTimeScale with `time_scale = 0.0`
- always set cooldown on feedbacks triggered by player input or rapid game events
- always use `UNSCALED` timing on the restore/recovery feedback after a time scale freeze
- call `restore_initial_values()` on scene reset or state transitions

## Output

Produce bounded feedback implementation work that includes:

- FeedbackPlayer composition (which feedbacks, in what order)
- shaker wiring if shake feedbacks are used
- GDScript trigger code
- timing and easing choices with rationale
- MCP sync or LSP steps used when editor-connected
- validation performed and its limits
- exact blocker and next action if blocked

## Quality Gates

- Feedbacks are layered in one FeedbackPlayer per event, not scattered across separate players.
- Shake feedbacks have matching shaker nodes with correct channel wiring.
- Feedbacks that must run during hit pauses use `UNSCALED` timing.
- Cooldowns are set on rapid-fire triggers.
- Intensity is used for proportional effects rather than duplicating FeedbackPlayers at different strengths.
- The implementation is grounded in actual project artifacts.
- `restore_initial_values()` is called on scene cleanup.

## Failure Modes

- Do not create separate FeedbackPlayers for each individual effect in one event.
- Do not use shake feedbacks without wiring a matching shaker node.
- Do not forget `UNSCALED` timing on feedbacks that must survive a time scale freeze.
- Do not hardcode effect strength when intensity parameter would make it proportional.
- Do not skip cooldowns on feedbacks triggered by rapid input.
- Do not make feel design decisions during implementation — escalate to `godot-architect`.
- Do not use FBProperty when a specialized feedback (Scale, Position, Rotation, Flash) exists.
- Do not forget to set `_playing = false` in custom feedbacks — FBHoldingPause will hang forever.
- Do not forget to register custom feedbacks in `editor/feedback_inspector.gd` FEEDBACK_MENU for inspector visibility.

## References

Read only as needed:

- `references/feedback-catalog.md` — all feedback types, shaker wiring, BeatPlayer, range attenuation, API
- `references/traps-and-internals.md` — debugging, gotchas, custom feedback lifecycle, channel issues
- `references/examples-and-validation.md` — worked examples and validation checklists
- `../../foundation/Godot Nuanced Development Practices.md`
