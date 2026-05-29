# Feedback Catalog

Quick reference for all 18 GodotFeel feedbacks. Use this to pick the right feedbacks for a sequence.

## Transform Feedbacks

Tween a node's transform property from A to B. All support `relative` mode and configurable easing.

| Feedback | Class | What it does | Typical use |
|----------|-------|-------------|-------------|
| Position 2D | `FBPosition2D` | Tweens position | Bump on hit, recoil, knockback |
| Position 3D | `FBPosition3D` | Tweens position | 3D knockback, recoil |
| Scale 2D | `FBScale2D` | Tweens scale | Button pop, damage squash, collect burst |
| Scale 3D | `FBScale3D` | Tweens scale | 3D squash and stretch |
| Rotation 2D | `FBRotation2D` | Tweens rotation | Spin on collect, tilt on damage |
| Rotation 3D | `FBRotation3D` | Tweens rotation | 3D spin, tilt |

Key properties: `target_path`, `from_*`, `to_*`, `duration`, `tween_type`, `ease_type`.

### Easing-to-Game-Feel Reference

| Easing combo | Feel | Best for |
|---|---|---|
| `TRANS_BACK + EASE_OUT` | Punch, anticipation, overshoot | Scale pops, knockback, button press |
| `TRANS_ELASTIC + EASE_OUT` | Springy, rubbery, magical | Pickup bounces, UI wobble, collect effects |
| `TRANS_BOUNCE + EASE_OUT` | Hard settle, impact | Landing, heavy drops, slam |
| `TRANS_QUAD + EASE_IN_OUT` | Smooth, neutral | Position slides, fades, gentle transitions |
| `TRANS_CUBIC + EASE_OUT` | Snappy deceleration | Menu transitions, subtle slides |

Multi-stage pops can chain these: overshoot with `BACK`, wobble with `ELASTIC`, settle with `BOUNCE`.

### Intensity Scaling (opt-in)

Transform feedbacks (Scale, Position, Rotation) have a `scale_by_intensity` export (default `false`). When enabled, the `to_*` value is lerped from `from_*` based on the intensity passed to `play()`. This makes effects proportional to damage, speed, or force without duplicating feedbacks at different strengths.

## Shake Feedbacks

Shakes are **triggers** — they emit a signal through `FeedbackBus`. A matching **Shaker** node on the target receives it.

| Feedback | Class | What it does | Typical use |
|----------|-------|-------------|-------------|
| Camera Shake | `FBCameraShake` | Shakes camera via bus | Explosions, heavy impacts |
| Position Shake | `FBPositionShake` | Shakes node position | Enemy hit reaction, UI shake |
| Scale Shake | `FBScaleShake` | Shakes node scale | Heartbeat, tension |

Key properties: `channel`, `intensity`, `duration`.

### Shaker Wiring

Shake feedbacks do nothing without a matching shaker node:

```
Camera2D
  └── CameraShaker (channel: "default")

EnemySprite
  └── PositionShaker (channel: "enemy")

UIPanel
  └── ScaleShaker (channel: "ui")
```

The feedback's `channel` must match the shaker's `channel`. One feedback can trigger all shakers on the same channel — an explosion can shake camera + enemies + UI simultaneously.

### Shaker Properties

| Property | Default | Description |
|----------|---------|-------------|
| `channel` | `&"default"` | Must match the feedback's channel exactly (StringName, case-sensitive) |
| `shake_duration` | `0.2` | Duration (overridden by signal each time) |
| `play_on_ready` | `false` | Shake immediately when entering the scene |
| `interruptible` | `true` | New shake replaces current (no additive stacking) |
| `cooldown` | `0.0` | Min seconds between shakes (wall-clock time) |
| `always_reset_after_shake` | `true` | Auto-restore node to initial values when shake ends |

### Shaker Behavior Details

- **CameraShaker**: Frequency-based sampling (default 25 Hz) — stepped/jittery shake, not smooth per-frame noise. Camera3D offsets scaled by 0.01 (world units). Detects Camera2D/3D parent automatically.
- **PositionShaker**: Random offset × amplitude × intensity × linear decay. 3D: Z axis is never shaken (`Vector3(offset.x, offset.y, 0.0)`).
- **ScaleShaker**: Random jitter × amplitude × intensity × linear decay. 3D: each axis gets independent random jitter (non-uniform).
- **All shakers**: Linear decay only (`1.0 - progress`). Signal parameters override exported values each time. Use `_process(delta)` so they slow during slow-mo.

## Audio / Visual Feedbacks

| Feedback | Class | What it does | Typical use |
|----------|-------|-------------|-------------|
| Sound | `FBSound` | Plays an AudioStream | Hit sounds, UI clicks, pickups |
| Flash | `FBFlash` | Modulates color briefly | Damage flash, invincibility blink |
| Particles | `FBParticles` | Triggers GPUParticles2D/3D | Sparks, dust, blood splatter |

### FBFlash — modulate vs self_modulate

`modulate` tints the node **and all child CanvasItems** (sprites, labels, particles). `self_modulate` tints **only the node itself**, leaving children untouched. FBFlash defaults to `modulate` (whole subtree flashes together). Set `use_self_modulate = true` when the target has children that should not flash — e.g., a character sprite with child health bars or particle effects.

When using FBProperty to tween color, the same distinction applies: use property path `modulate` or `modulate:a` for subtree-wide effects, `self_modulate` or `self_modulate:a` for leaf-only effects.

## Animation Feedbacks

| Feedback | Class | What it does | Typical use |
|----------|-------|-------------|-------------|
| Animation | `FBAnimation` | Plays AnimationPlayer clip | Complex multi-track effects |
| Property | `FBProperty` | Tweens any property | Anything not covered above |
| Spring | `FBSpring` | Damped harmonic oscillator | Bouncy UI, elastic snap-back |

### FBProperty — Swiss Army Knife

Tweens any property on any node. Set `target_path`, `property_name` (e.g., `modulate:a`, `material:shader_parameter/intensity`), `property_type` (Float, Vector2, Vector3, Color), and from/to values. Use only when no specialized feedback exists.

### FBSpring — Organic Motion

Key parameters:
- `damping` (0–1): lower = more bouncy. Default 0.3.
- `frequency`: higher = snappier. Default 15.
- `amplitude`: displacement from rest.

Works on position, scale, or rotation.

## Time / Flow Feedbacks

| Feedback | Class | What it does | Typical use |
|----------|-------|-------------|-------------|
| Time Scale | `FBTimeScale` | Slows/freezes time | Hit pause, dramatic moments |
| Pause | `FBPause` | Waits N seconds | Stagger between phases |
| Holding Pause | `FBHoldingPause` | Waits for all above to finish | Sync point before next phase |

### Sequencing with Pauses

All feedbacks in a player fire simultaneously by default. Use pauses to create phases:

```
Phase 1 (simultaneous):
  - Scale punch
  - Flash
  - Sound

Holding Pause  ← waits for Phase 1 to finish

Phase 2 (simultaneous):
  - Position slide
  - Particles
```

Use `Initial Delay` on individual feedbacks for slight staggers within a phase — often cleaner than adding Pause feedbacks.

## Timing Controls

Every feedback has a `FeedbackTiming` resource:

| Setting | What it does | When to use |
|---------|-------------|-------------|
| `Initial Delay` | Wait before playing | Stagger without Pause feedbacks |
| `Cooldown` | Min seconds between plays | Prevent spam on rapid triggers |
| `Number of Repeats` | Play N extra times | Pulsing, flickering |
| `Repeat Forever` | Never stop | Ambient loops |
| `Delay Between Repeats` | Gap between repeats | Control pulse rhythm |
| `Limit Play Count` | Stop after N total | One-shot effects |
| `Timescale Mode` | SCALED or UNSCALED | UNSCALED for effects during slow-mo |
| `Constant Intensity` | Ignore intensity param | Always-full-strength effects |
| `Chance` | 0–100% probability | Randomized variation |
| `Direction Condition` | Only forwards or backwards | Asymmetric sequences |

## Intensity and Range

### Intensity

`play(intensity)` accepts 0.0–1.0. Shake feedbacks multiply their values by intensity. Use to scale effects proportionally to damage, speed, or force.

```gdscript
$HitFeedback.play(0.3)   # light hit
$HitFeedback.play(1.0)   # heavy hit
```

### Range Attenuation (FeedbackRange)

`FeedbackRange` is a Resource you assign to any feedback's `range_settings` property. When assigned, the feedback's intensity is multiplied by distance-based attenuation. If the result is ≤ 0, the feedback is skipped entirely.

**Properties:**
| Property | Default | Description |
|----------|---------|-------------|
| `range_distance` | `10.0` | Max distance at which the feedback has any effect |
| `falloff_curve` | `null` | Optional Curve resource. X = normalized distance (0=close, 1=far), Y = intensity multiplier. Null = linear falloff |

**How position works:**
- Pass position as Vector3 to `play(intensity, position)`
- For 2D nodes, the FeedbackPlayer auto-converts `Node2D.global_position` to `Vector3(x, y, 0.0)`
- Attenuation = `1.0 - (distance / range_distance)` (linear) or `1.0 - curve.sample(t)` (custom)

```gdscript
# Explosion at a position — nearby enemies get full shake, far ones get less
$ExplosionFeedback.play(1.0, Vector3(explosion_pos.x, explosion_pos.y, 0.0))
```

**Use cases:** Explosions, area-of-effect impacts, environmental shakes that fade with distance.

## BeatPlayer

A standalone **Node** (not a Resource) that triggers a FeedbackPlayer on beat at a given BPM.

### Setup
```
SomeParent
  ├── FeedbackPlayer "PulseFeedback"
  └── BeatPlayer (feedback_player_path → ../PulseFeedback)
```

### Properties

| Property | Default | Description |
|----------|---------|-------------|
| `feedback_player_path` | — | NodePath to the FeedbackPlayer to trigger |
| `bpm` | `120.0` | Beats per minute |
| `auto_start` | `false` | Start playing on `_ready()` |
| `loop` | `true` | Restart pattern when it ends |
| `beat_count` | `0` | Beats before stopping (0 = infinite when `loop = true`) |
| `beat_pattern` | `[]` | Intensity per beat. Empty = 1.0 for all beats |

### Signal
- `beat_fired(beat_index: int)` — emitted on each beat, useful for syncing other systems.

### Usage
```gdscript
$BeatPlayer.bpm = 128.0
$BeatPlayer.beat_pattern = [1.0, 0.5, 0.8, 0.5]  # intensity cycles per beat
$BeatPlayer.start()

# Stop later
$BeatPlayer.stop()
```

### Notes
- Pattern cycles: index = `current_beat % beat_pattern.size()`
- Beats with intensity 0.0 are skipped (FeedbackPlayer not called)
- Uses `_process(delta)` — affected by `Engine.time_scale`
- Good for: rhythm games, music visualizers, ambient pulsing, heartbeat effects

## Channel Debugging

When shake feedbacks don't work, enable bus debugging:

```gdscript
FeedbackBus.debug = true
```

This logs channel registrations and emits `push_warning()` when a shake fires on a channel with no listeners — catches typos immediately. Zero-cost when disabled.

**Best practice** — define channels as constants:
```gdscript
class_name Channels
const CAMERA := &"camera"
const PLAYER_HIT := &"player_hit"
const UI_SHAKE := &"ui_shake"
```

Channels are `StringName`, exact match, case-sensitive. No wildcards.

## Runtime API Quick Reference

```gdscript
$FeedbackPlayer.play()                    # play at full intensity
$FeedbackPlayer.play(0.5)                 # play at half intensity
$FeedbackPlayer.play(1.0, position)       # play with range attenuation
$FeedbackPlayer.stop()                    # stop all active feedbacks
$FeedbackPlayer.restore_initial_values()  # reset targets to pre-play state
$FeedbackPlayer.reset_feedbacks()         # reset play counts and state

# await completion
$FeedbackPlayer.play()
await $FeedbackPlayer.completed
queue_free()

# add feedback from code
var fb := FBScale2D.new()
fb.target_path = NodePath("../Sprite2D")
fb.to_scale = Vector2(2.0, 2.0)
fb.duration = 0.2
$FeedbackPlayer.add_feedback(fb)

# global kill switch
FeedbackPlayer.global_active = false
```

## Common Recipes

### Hit Impact
```
1. FBScale2D — squash: to_scale (0.8, 1.2), 0.05s
2. FBFlash — white, 0.05s
3. FBCameraShake — intensity 3, 0.1s
4. FBTimeScale — freeze: time_scale 0.0, 0.03s
5. FBSound — hit sound
6. FBScale2D — bounce back: to_scale (1, 1), 0.15s, TRANS_BACK EASE_OUT
```

### Button Press
```
1. FBScale2D — shrink: to_scale (0.9, 0.9), 0.05s
2. FBSound — click
3. FBScale2D — overshoot: to_scale (1.05, 1.05), 0.1s, TRANS_BACK
4. FBScale2D — settle: to_scale (1, 1), 0.08s
```

### Pickup Collect
```
1. FBScale2D — pop: to_scale (1.5, 1.5), 0.1s
2. FBFlash — gold
3. FBParticles — sparkle burst
4. FBSound — chime
5. FBScale2D — shrink to nothing: to_scale (0, 0), 0.15s
```
