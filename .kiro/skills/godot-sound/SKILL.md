---
name: godot-sound
description: |
  Implement audio playback in Godot 4 using the Sound Manager addon (nathanhoad/godot_sound_manager).
  Play sound effects, UI sounds, ambient sounds, and music with crossfade support using the
  SoundManager autoload singleton and its pooled AudioStreamPlayer architecture.
  Use when the user needs to add sound effects to gameplay, play background music, crossfade
  between music tracks, manage ambient soundscapes, control audio volume per subsystem,
  set up audio buses, or integrate the Sound Manager plugin into a Godot project.
  Also use when someone mentions SoundManager, play_sound, play_music, play_ambient_sound,
  play_ui_sound, crossfade, audio bus, sound volume, music volume, ambient volume,
  AudioStreamPlayer pooling, sound_effects, ui_sound_effects, ambient_sounds,
  or asks to "add sounds," "play music," "fade between tracks," "add background audio,"
  "make this button click," or "set up audio."
  Even if the user just says "add sound to this game" or "I need music" or "play a sound
  when the player gets hit" in a Godot project, this skill applies.
  Do NOT use for game feel/juice feedback sequences (use godot-feel), dialogue audio cues
  (use godot-dialogue), or general GDScript unrelated to audio (use godot-gdscript).
---

# Godot Sound

Implement audio playback using the Sound Manager addon with correct API usage, bus configuration, and pool-aware patterns.

Read `references/api-reference.md` before writing any SoundManager calls. Read `references/setup-and-patterns.md` for installation, bus layout, and common patterns.

## Responsibility

- Wire SoundManager API calls in GDScript for sound effects, UI sounds, ambient sounds, and music.
- Configure audio bus layout to match the plugin's bus auto-detection expectations.
- Set up the Sound Manager plugin (autoload registration, plugin enablement) when not already present.
- Manage volume controls per subsystem (sounds, UI, ambient, music).
- Use crossfade for music transitions.
- Handle ambient sound lifecycles with fade-in/fade-out.
- Set process modes when audio must behave differently during pause.
- Validate that audio resources exist and bus names resolve correctly.

## Use When

- The task requires playing sound effects, UI sounds, ambient sounds, or music.
- Music needs crossfading, pause/resume, or track history queries.
- Ambient sounds need fade-in/fade-out lifecycle management.
- Audio volume needs per-subsystem control.
- The Sound Manager plugin needs installation or configuration.
- Audio bus layout needs setup or modification.
- Process modes need adjustment for pause behavior.

## Do Not Use When

- The task is game feel/juice feedback sequences that happen to include sound (use `godot-feel`; it handles FBSound feedbacks).
- The task is dialogue audio cues managed by Dialogue Manager (use `godot-dialogue`).
- Architecture decisions about audio design are still open (escalate to `godot-architect`).
- The task is general GDScript unrelated to audio (use `godot-gdscript`).
- The task is about save/load of audio settings (escalate to `godot-persistence`, then return here for playback).

## Core Mental Model

Sound Manager is a single autoload singleton that owns four independent audio player pools:

```
SoundManager (autoload singleton)
  +-- sound_effects      (SoundEffectsPlayer, pool=8, bus: Sounds/SFX, pausable)
  +-- ui_sound_effects   (SoundEffectsPlayer, pool=8, bus: UI/Interface/Sounds/SFX, always)
  +-- ambient_sounds     (AmbientSoundsPlayer, pool=1, bus: Sounds/SFX, always)
  +-- music              (MusicPlayer, pool=2, bus: Music, always)
```

Each pool manages AudioStreamPlayer nodes that are recycled after playback. Pools auto-expand when exhausted and shrink back to default size. The plugin auto-detects audio buses by trying candidate names in multiple case formats against AudioServer.

Key distinctions between subsystems:
- Sound effects: fire-and-forget, pauses with game by default
- UI sounds: fire-and-forget, never pauses (runs during pause menus)
- Ambient sounds: continuous, prevents duplicate playback of same resource, supports fade-in/fade-out
- Music: crossfade between tracks, pause/resume, 50-entry track history

## Workflow

1. Confirm the Sound Manager addon is present in the project (`addons/sound_manager/plugin.cfg`). Check that the `SoundManager` autoload is registered. If missing, guide installation (see `references/setup-and-patterns.md`).

2. Read `references/api-reference.md` to ground yourself in the exact method signatures before writing any SoundManager calls.

3. Identify what the task requires:
   - **Sound effects**: `SoundManager.play_sound(resource)` or `SoundManager.play_ui_sound(resource)`.
   - **Music**: `SoundManager.play_music(resource, crossfade)` with optional position, volume, crossfade duration.
   - **Ambient sounds**: `SoundManager.play_ambient_sound(resource, fade_in)` and `SoundManager.stop_ambient_sound(resource, fade_out)`.
   - **Volume control**: `SoundManager.set_sound_volume(v)`, `set_music_volume(v)`, etc. Volume is 0.0 to 1.0.
   - **Bus setup**: Ensure `default_bus_layout.tres` has the expected buses (Master, Sounds, Music). The plugin auto-detects buses by candidate name.
   - **Process modes**: Adjust `sound_process_mode`, `ui_sound_process_mode`, `ambient_sound_process_mode`, `music_process_mode` if default pause behavior is wrong.

4. Load audio resources correctly:
   ```gdscript
   # Preload for known resources
   const HIT_SFX: AudioStream = preload("res://assets/audio/hit.wav")
   
   # Or load dynamically
   var track: AudioStream = load("res://assets/audio/music/battle.ogg")
   ```

5. Wire playback calls in the appropriate game scripts:
   ```gdscript
   # Sound effect (fire-and-forget)
   SoundManager.play_sound(HIT_SFX)
   
   # UI sound (plays during pause)
   SoundManager.play_ui_sound(CLICK_SFX)
   
   # Music with 2-second crossfade
   SoundManager.play_music(BATTLE_MUSIC, 2.0)
   
   # Ambient with 1-second fade-in
   SoundManager.play_ambient_sound(RAIN_AMBIENT, 1.0)
   
   # Stop ambient with 2-second fade-out
   SoundManager.stop_ambient_sound(RAIN_AMBIENT, 2.0)
   ```

6. For music transitions, understand the crossfade mechanic:
   - Calling `play_music()` with a crossfade value automatically fades out the current track and fades in the new one.
   - The fade-out duration is 2x the crossfade value (old track fades out over 4s when crossfade=2.0).
   - Fade-out uses `TRANS_CIRC / EASE_IN`, fade-in uses `TRANS_QUAD / EASE_OUT`.
   - `stop_music(fade_out)` fades out all playing music.

7. For ambient sounds, remember:
   - The plugin prevents duplicate playback of the same resource. Calling `play_ambient_sound()` with an already-playing resource is a no-op.
   - Looping depends on the AudioStream resource's own loop setting, not the plugin. Ensure `.wav` or `.ogg` resources have loop enabled in import settings.
   - `stop_all_ambient_sounds(fade_out)` fades out every active ambient sound.

8. Write files to disk using native file editing. After writing `.gd` files, call `refresh_filesystem` to sync the editor.

9. Validate:
   - In editor-connected mode: use `get_diagnostics` on changed scripts. Verify audio resources exist at the referenced paths.
   - Check that audio bus names in `default_bus_layout.tres` match what the plugin expects (Sounds, Music, or custom names set via `set_default_sound_bus()`).
   - If a test scene exists, use `run_scene` to verify no runtime errors.

10. Escalate if needed:
    - Audio design decisions (what sounds where, volume balancing strategy) -> `godot-architect`
    - Game feel feedback sequences that include sound -> `godot-feel`
    - Save/load of volume settings -> `godot-persistence`
    - General GDScript outside audio -> `godot-gdscript`

## Output

- GDScript with correct SoundManager API calls for the required audio playback.
- Audio bus layout changes if needed (`default_bus_layout.tres`).
- Plugin setup guidance if Sound Manager is not yet installed.
- Volume control wiring if the task involves settings UI.
- Validation results and any remaining blockers.

## Quality Gates

- All SoundManager method calls use correct signatures from `references/api-reference.md`.
- Audio resources are loaded with correct paths and types (`AudioStream`).
- Sound effects use `play_sound()` for game sounds and `play_ui_sound()` for UI sounds (not interchangeable when pause behavior matters).
- Music crossfade uses `play_music(resource, crossfade_duration)`, not manual tween hacks.
- Ambient sounds that should loop have loop enabled on the AudioStream resource itself.
- Volume values are in the 0.0 to 1.0 range (the plugin converts to dB internally).
- Audio bus names in the project match the plugin's candidate list or are set explicitly via `set_default_*_bus()`.
- `refresh_filesystem` is called after disk writes before editor validation.
- Typed GDScript throughout (variables, parameters, return types).

## Failure Modes

- Do not call `AudioServer` directly for playback when SoundManager handles it. The plugin manages pooling, bus routing, and fading internally.
- Do not create raw `AudioStreamPlayer` nodes manually when SoundManager's pool should be used.
- Do not assume ambient sounds loop automatically. The loop setting lives on the AudioStream resource, not the plugin.
- Do not use `play_sound()` for UI sounds that must play during pause. Use `play_ui_sound()` instead (it runs in `PROCESS_MODE_ALWAYS`).
- Do not set volume using dB values directly. SoundManager's `set_*_volume()` methods expect 0.0 to 1.0 linear values.
- Do not call `play_music()` repeatedly for the same track expecting it to restart. Check `is_music_playing(resource)` first if restart behavior matters.
- Do not forget that `play_ambient_sound()` is a no-op if that resource is already playing. This is intentional duplicate prevention, not a bug.
- Do not invent SoundManager methods that do not exist. Refer to `references/api-reference.md`.
- Do not claim validation that was not performed.
- Do not call `get_last_played_music_track()` without first checking `get_music_track_history().size() > 0`. It crashes with index-out-of-bounds if no music has ever been played.
- Do not call `player.stop()` directly on a returned AudioStreamPlayer. Pool recycling depends on the `finished` signal — manual stops leave the player stuck in `busy_players`. Use `stop_sound()`, `stop_music()`, or `stop_ambient_sound()` instead.
- Do not call `queue_free()` on returned AudioStreamPlayers. They belong to the pool.
- Do not assume `stop_sound(resource)` works with dynamically `load()`ed resources. It uses reference equality (`==`), not path comparison. Always use `preload()` or a shared reference variable.
- Do not assume bus auto-detection picks up buses added after game start. It runs once at `_init()`. Use `set_default_*_bus()` for dynamically created buses.

## References

Read only as needed:

- `references/api-reference.md` -- complete GDScript and C# API tables
- `references/setup-and-patterns.md` -- installation, bus layout, common patterns, gotchas
- `../../foundation/Godot Nuanced Development Practices.md`
