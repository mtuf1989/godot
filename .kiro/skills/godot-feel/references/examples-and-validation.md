# Examples and Validation

## Example 1: Enemy Hit Feedback

**Request:** "Make the enemy flash and shake when hit"

**Sequence composition:**
```
FeedbackPlayer "HitFeedback" (child of Enemy)
  ├── FBFlash — target: ../Sprite2D, flash_color: white, duration: 0.05
  ├── FBScale2D — target: ../Sprite2D, to_scale: (0.85, 1.15), duration: 0.05
  ├── FBPositionShake — channel: "enemy", intensity: 5, duration: 0.1
  ├── FBSound — hit_sound.ogg
  └── FBScale2D — target: ../Sprite2D, from_scale: (0.85, 1.15), to_scale: (1, 1), duration: 0.15, TRANS_BACK EASE_OUT
```

**Shaker wiring:**
```
Enemy/Sprite2D
  └── PositionShaker (channel: "enemy")
```

**Trigger code:**
```gdscript
func take_damage(amount: int, from_position: Vector2) -> void:
    health -= amount
    $HitFeedback.play(clampf(float(amount) / max_health, 0.2, 1.0))
```

**Why intensity is proportional:** Light hits feel light, heavy hits feel heavy — one FeedbackPlayer handles both.

**Validation checklist:**
- [ ] PositionShaker exists as child of the shaken node
- [ ] Channel strings match between FBPositionShake and PositionShaker
- [ ] Cooldown set if enemy can be hit rapidly
- [ ] `restore_initial_values()` called on enemy death/despawn

## Example 2: Hit Pause with Camera Shake

**Request:** "Add a hit pause and screen shake on heavy attacks"

**Sequence composition:**
```
FeedbackPlayer "HeavyHitFeedback"
  ├── FBTimeScale — time_scale: 0.0, duration: 0.04
  ├── FBCameraShake — channel: "camera", intensity: 10, duration: 0.15
  └── FBSound — heavy_hit.ogg (timing: UNSCALED)
```

**Critical:** The sound feedback uses `UNSCALED` timing so it plays during the freeze. Without this, the sound waits until time resumes.

**Shaker wiring:**
```
Camera2D
  └── CameraShaker (channel: "camera")
```

**Validation checklist:**
- [ ] CameraShaker is a direct child of Camera2D/3D
- [ ] Any feedback that must play during the freeze has `Timescale Mode = UNSCALED`
- [ ] Cooldown set to prevent stacking multiple freezes

## Example 3: UI Button Juice

**Request:** "Make buttons feel snappy"

**Sequence composition:**
```
FeedbackPlayer "ButtonPressFeedback" (child of Button)
  ├── FBScale2D — target: .., to_scale: (0.92, 0.92), duration: 0.04
  ├── FBSound — click.ogg
  ├── FBHoldingPause
  ├── FBScale2D — target: .., to_scale: (1.04, 1.04), duration: 0.08, TRANS_BACK
  ├── FBHoldingPause
  └── FBScale2D — target: .., to_scale: (1, 1), duration: 0.06
```

**Why Holding Pauses:** The shrink → overshoot → settle must happen in sequence, not simultaneously. Each phase waits for the previous to finish.

**Trigger code:**
```gdscript
func _on_pressed() -> void:
    $ButtonPressFeedback.play()
```

## Validation Approach

### Editor-Connected

1. `get_diagnostics` on changed `.gd` files
2. Verify via `scene://current/tree`:
   - FeedbackPlayer exists as child of the triggering node
   - Shaker nodes exist as children of shaken targets
   - Channel strings match
3. `search_symbols` to confirm FeedbackPlayer, CameraShaker, etc. are recognized

### Source-Only

State what the user must verify manually:
- shaker node placement and channel matching
- FeedbackPlayer array contents (inspector-configured, not visible in script)
- timing mode on feedbacks that must survive time scale changes
