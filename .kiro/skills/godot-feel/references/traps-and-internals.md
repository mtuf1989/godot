# Traps, Gotchas & Internal Mechanics

Read this when debugging unexpected behavior, creating custom feedbacks, or wiring shakers.

## Table of Contents

1. [Setup Traps](#setup-traps)
2. [Feedback-Specific Traps](#feedback-specific-traps)
3. [Shaker Traps](#shaker-traps)
4. [Channel & FeedbackBus Traps](#channel--feedbackbus-traps)
5. [Timing Traps](#timing-traps)
6. [Custom Feedback Lifecycle](#custom-feedback-lifecycle)
7. [Inspector Gotchas](#inspector-gotchas)
8. [Debugging Techniques](#debugging-techniques)

---

## Setup Traps

**Plugin not enabled → silent failure.** If the GodotFeel plugin isn't enabled in Project Settings, the `FeedbackBus` autoload doesn't exist. All shake feedbacks silently do nothing — no errors, no warnings. Always verify: Project → Project Settings → Plugins → GodotFeel → ✅ Enabled.

**Feedbacks are Resources, not Nodes.** You cannot use `@export var target: Node2D` on a feedback — that's illegal on Resources. Always use `@export var target_path: NodePath` and resolve it in `_custom_init()`. A FeedbackPlayer with 10 feedbacks is 1 node in the scene tree, not 11.

**Resource sharing is usually a bug.** Because feedbacks are Resources, Godot can share instances between FeedbackPlayers if you duplicate or copy-paste. Each feedback holds internal state (`_playing`, `_active_tween`, timing counters). Shared instances corrupt each other. Always ensure feedbacks are unique instances — the ＋ Add Feedback button creates unique instances automatically.

**How sharing corrupts state:** When two FeedbackPlayers share the same Feedback resource instance, both players write to the same `_playing` flag and `_active_tween` reference. Player A calls `play()`, sets `_playing = true`, and creates a tween. Player B calls `play()` on the same feedback, `_create_tween()` kills Player A's tween, and now Player A's FBHoldingPause waits forever because `_playing` was reset by Player B's tween finishing. Symptom: "feedback works on one enemy but not the other" or "sequence hangs randomly." The addon now emits a `push_warning()` at runtime when it detects a shared feedback — check the Output panel for "Feedback is shared between FeedbackPlayers."

**Common sharing vectors:** Node duplication (Ctrl+D), copy-paste of FeedbackPlayer nodes, scene instancing where feedbacks are built-in resources. The ＋ Add Feedback button and creating feedbacks from code (`FBScale2D.new()`) always produce unique instances.

---

## Feedback-Specific Traps

### One Writer Per Property Per Phase
Never have two feedbacks targeting the same property on the same node in the same phase. Godot warns that animating the same property with multiple tweens simultaneously produces undefined results — the last created tween wins. FBFlash writes `modulate` directly; FBProperty can tween `modulate`. If both fire simultaneously, the result is a fight between the tween and the direct write each frame. Use FBHoldingPause to separate them into phases, or use `self_modulate` for one of them (FBFlash supports `use_self_modulate`).

### FBTimeScale
- **Affects the ENTIRE game.** Every node, tween, timer, and physics step. Ensure UI and critical systems handle `Engine.time_scale` changes.
- **Overlapping freezes break restore.** If two FBTimeScale feedbacks overlap, the second one's restore sets `time_scale = 1.0` even if the first hasn't finished. Never overlap time scale effects.
- **Always uses UNSCALED internally.** The `timescale_mode` property is hidden in the inspector for FBTimeScale — you can't accidentally set it to SCALED. If time_scale is 0, a SCALED feedback could never restore it.

### FBFlash
- **Timing now respects `timescale_mode`.** FBFlash uses `_wait_seconds()` internally, which honors the UNSCALED setting. Set `timescale_mode = UNSCALED` on FBFlash when it must play during a hit pause (FBTimeScale with `time_scale = 0.0`). Without UNSCALED, the flash freezes along with everything else.
- **Freed-during-await is safe.** The flash loop checks `is_instance_valid()` after each await.
- **`use_self_modulate` for leaf-only flashes.** When `true`, writes to `self_modulate` instead of `modulate`, so child CanvasItems (health bars, particles, labels) are not tinted. Default is `false` (whole subtree flashes).

### FBPosition2D / FBPosition3D
- **`relative = true` + rapid re-trigger = offset accumulation.** Each play starts from the *current* position, not the initial one. Rapid triggers drift the node away from its origin.

### FBScale2D / FBScale3D
- **No clamping on `Vector2(0, 0)`.** Scale zero makes the node invisible and may cause physics issues. The feedback doesn't prevent this.

### FBProperty
- **`value_type` must match the actual property type.** Tweening a `Color` property with `value_type = Float` produces undefined behavior. No runtime check.
- **Typo detection exists.** If `property_name` doesn't exist on the target, a `push_warning()` is emitted and the feedback skips gracefully.

### FBSpring
- **High frequency + low damping = flickering.** Looks like jitter, not bouncing. Start conservative: frequency 8–12, damping 0.3–0.5.
- **Fixed duration.** The spring doesn't auto-detect when oscillation becomes imperceptible. Set duration long enough for the decay to complete visually.

### FBParticles
- **`one_shot = false` + `auto_stop = false` = emit forever.** The feedback sets `emitting = true` but never stops it. Either set `one_shot = true` on the particle node or `auto_stop = true` on the feedback.

### FBSound
- **Creates a persistent AudioStreamPlayer child.** Each FBSound creates one during `_custom_init()`. Many FBSound feedbacks = many child nodes. No pooling.
- **AudioStreamPlayer is a child of the FeedbackPlayer's owner.** If the owner is freed, the player node is orphaned.

### FBAnimation
- **External interruption is low-risk.** If another script calls `play()` on the same AnimationPlayer, the `animation_finished` signal may fire for a different animation. The callback ignores the animation name, so this rarely causes issues.

### FBHoldingPause
- **Hangs if a previous feedback never sets `_playing = false`.** The holding pause waits forever. This is the #1 symptom of a buggy custom feedback.

---

## Shaker Traps

**No additive shaking.** If `interruptible = true` (default), a new shake request restarts from scratch, re-grabbing initial values. Two simultaneous shakes on the same shaker don't stack — the second replaces the first.

**Signal parameters override exports.** The amplitude/frequency values from the feedback signal overwrite the shaker's exported values each time. If multiple feedbacks trigger the same shaker with different parameters, the last one wins.

**Linear decay only.** All shakers use `decay = 1.0 - progress`. No curve or easing option on the shaker side. Custom decay requires modifying shaker code.

**Shakers use `_process(delta)` — affected by time_scale.** During slow-mo, shakes slow down proportionally. Setting `process_mode = PROCESS_MODE_ALWAYS` makes the shaker tick during pauses but the delta is still scaled, so shake duration will be wrong. There's no built-in unscaled shake mode.

**CameraShaker frequency-based sampling.** Default 25 Hz — generates a new random offset only at the sample interval, creating stepped/jittery shake. The `frequency` parameter from the feedback signal overrides the shaker's exported frequency each time.

**CameraShaker 3D scaling.** Camera3D offsets are scaled by 0.01 because `h_offset`/`v_offset` are in world units, not pixels.

**PositionShaker 3D: Z axis is never shaken.** The 2D offset maps to `Vector3(offset.x, offset.y, 0.0)`.

**ScaleShaker 3D: fully non-uniform.** Each axis (X, Y, Z) gets an independent random jitter value.

**Wrong parent type now warns.** If a CameraShaker's parent isn't Camera2D/3D, or PositionShaker/ScaleShaker's parent isn't Node2D/3D, a `push_warning()` fires at `_ready()`.

**`always_reset_after_shake` defaults to `true`.** Shakers auto-restore the node when shake ends. Set to `false` only if you intentionally want the node to stay at its last shaken offset.

---

## Channel & FeedbackBus Traps

**Channels are `StringName` — exact match, case-sensitive.** `&"Camera"` ≠ `&"camera"`. No wildcards or pattern matching.

**No shaker on channel = silent failure.** If no shaker exists with a matching channel, nothing happens. No error in normal mode.

**Enable `FeedbackBus.debug = true` for typo detection.** In debug mode:
- Logs each channel registration when a shaker connects
- Emits `push_warning()` when a shake fires on a channel with no registered listeners
- Zero-cost when disabled

**Best practice — define channels as constants:**
```gdscript
class_name Channels
const CAMERA := &"camera"
const PLAYER_HIT := &"player_hit"
const UI_SHAKE := &"ui_shake"
```

**FeedbackBus must exist.** Shakers check for `/root/FeedbackBus` via `get_node_or_null()`. If the plugin isn't enabled, the autoload doesn't exist and all shaker feedbacks silently fail.

---

## Timing Traps

**Cooldown uses wall-clock time** (`Time.get_ticks_msec()`). Cooldowns are always real-time regardless of `timescale_mode`. This means cooldowns work correctly during slow-mo.

**`initial_delay` and `delay_between_repeats` respect `timescale_mode`.** UNSCALED uses tick-based real-time waiting. SCALED uses `create_timer()` and slows during slow-mo.

**`limit_play_count` persists across `play()` calls.** If `max_play_count = 3`, the feedback plays at most 3 times total across all invocations. Call `reset_feedbacks()` on the FeedbackPlayer to reset the counter.

**FeedbackPlayer computes true unscaled delta from `Time.get_ticks_usec()`.** Godot's `_process(delta)` is always affected by `Engine.time_scale`, even with `PROCESS_MODE_ALWAYS`. The player works around this.

---

## Custom Feedback Lifecycle

When creating a custom feedback, extend `Feedback` and override these methods:

```
_custom_init(owner_player: Node)           → Called once at FeedbackPlayer._ready()
_custom_play(position: Vector3, intensity: float) → Called each time play() fires
_process_feedback(delta: float)            → Called every frame while _playing == true
_custom_stop(position: Vector3, intensity: float) → Called when stop() is invoked
_custom_restore_initial_values()           → Called when restore_initial_values() is invoked
_custom_skip_to_end(position, intensity)   → Called to jump to end state
_custom_reset()                            → Called to reset internal state
```

**Critical rules:**
- You MUST set `_playing = false` when your feedback finishes. The player polls this flag. If you forget, the feedback is "still playing" forever and FBHoldingPause hangs.
- Use `_create_tween()` helper — it kills any existing tween first, preventing leaks.
- Use `_wait_seconds(seconds)` helper — it respects `timescale_mode` automatically.
- Use `_kill_tween()` in `_custom_stop()` and `_custom_reset()`.
- Check `is_instance_valid(_owner)` after any await.

**Registration:** Custom feedback subclasses don't auto-appear in the inspector's ＋ dropdown. You must manually add them to the `FEEDBACK_MENU` dictionary in `editor/feedback_inspector.gd`.

---

## Inspector Gotchas

- **No delete button** in the custom sequence UI. Use the default array editor below the overview.
- **No reordering UI.** Drag items in the default array editor or reorder via script.
- **Colored chips** represent each feedback type. Inactive feedbacks (`active = false`) appear darkened.
- **Undo/Redo supported** for add operations via `EditorUndoRedoManager`.
- **Overview auto-refreshes** when the feedbacks array changes.

---

## Debugging Techniques

### Channel Issues
1. Set `FeedbackBus.debug = true`
2. Play the feedback
3. Check Output panel for:
   - "No listeners on channel X" → channel typo or missing shaker
   - Channel registration logs → verify shaker connected

### Feedback Not Playing
Check in order:
1. `FeedbackPlayer.global_active` is `true` (static kill switch)
2. Plugin is enabled (FeedbackBus autoload exists)
3. Feedback's `active` property is `true`
4. `chance` is 100 (or roll succeeded)
5. Not in cooldown
6. `limit_play_count` hasn't been exhausted
7. `intensity_interval` range includes the passed intensity
8. `direction_condition` matches current play direction
9. `range_settings` attenuation didn't reduce intensity to 0

### Sequence Hanging
- A feedback never set `_playing = false` — check custom feedbacks
- FBHoldingPause waiting on a stuck feedback
- A tween target was freed mid-tween (check `is_instance_valid`)

### Shaker Not Responding
1. Verify shaker is a child of the correct node type
2. Verify channel string matches exactly (case-sensitive)
3. Check `FeedbackBus.debug = true` output
4. Verify shaker isn't in cooldown
5. If `interruptible = false`, verify no shake is already active
