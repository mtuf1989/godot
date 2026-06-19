# Sound Manager API Reference

Complete method reference for the SoundManager autoload singleton. All methods are globally accessible via `SoundManager` in GDScript.

## Table of Contents

- [Sound Effects](#sound-effects)
- [UI Sounds](#ui-sounds)
- [Ambient Sounds](#ambient-sounds)
- [Music](#music)
- [Process Mode Properties](#process-mode-properties)
- [Return Value Usage](#return-value-usage)

## Sound Effects

Fire-and-forget game-world sounds. Pauses with the game by default (`PROCESS_MODE_PAUSABLE`).

| Method | Returns | Description |
|--------|---------|-------------|
| `play_sound(resource: AudioStream, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play a sound effect |
| `play_sound_with_pitch(resource: AudioStream, pitch: float, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play at specific pitch scale |
| `stop_sound(resource: AudioStream) -> void` | void | Stop all players matching this resource |

**Sound effects gotchas:**
- `stop_sound()` uses object reference equality (`player.stream == resource`), not resource path comparison. If you `preload()` the same file in multiple scripts, they share the same object and `stop_sound()` works. But if you `load()` at runtime or duplicate the resource, `stop_sound()` won't find the player. Always use `preload()` or store a shared reference.
| `get_sound_volume() -> float` | 0.0 to 1.0 | Current sound effects volume |
| `set_sound_volume(volume: float) -> void` | void | Set volume (0.0 to 1.0) |
| `set_default_sound_bus(bus: String) -> void` | void | Override the auto-detected bus |

## UI Sounds

Fire-and-forget UI sounds. Never pauses (`PROCESS_MODE_ALWAYS`). Use for button clicks, menu navigation, notifications.

| Method | Returns | Description |
|--------|---------|-------------|
| `play_ui_sound(resource: AudioStream, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play a UI sound |
| `play_ui_sound_with_pitch(resource: AudioStream, pitch: float, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play at specific pitch scale |
| `stop_ui_sound(resource: AudioStream) -> void` | void | Stop all players matching this resource |
| `get_ui_sound_volume() -> float` | 0.0 to 1.0 | Current UI sound volume |
| `set_ui_sound_volume(volume: float) -> void` | void | Set volume (0.0 to 1.0) |
| `set_default_ui_sound_bus(bus: String) -> void` | void | Override the auto-detected bus |

## Ambient Sounds

Continuous background audio. Never pauses. Prevents duplicate playback of the same resource. Supports fade-in on play and fade-out on stop.

| Method | Returns | Description |
|--------|---------|-------------|
| `play_ambient_sound(resource: AudioStream, fade_in: float = 0.0, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play ambient sound with optional fade-in duration |
| `stop_ambient_sound(resource: AudioStream, fade_out: float = 0.0) -> void` | void | Stop specific ambient sound with optional fade-out |
| `stop_all_ambient_sounds(fade_out: float = 0.0) -> void` | void | Stop all ambient sounds with optional fade-out |
| `get_ambient_sound_volume() -> float` | 0.0 to 1.0 | Current ambient volume |
| `set_ambient_sound_volume(volume: float) -> void` | void | Set volume (0.0 to 1.0) |
| `set_default_ambient_sound_bus(bus: String) -> void` | void | Override the auto-detected bus |

**Ambient gotchas:**
- `play_ambient_sound()` is a no-op if the same resource is already playing. This is intentional duplicate prevention.
- Looping depends on the AudioStream resource's own loop setting. The plugin does not set loop mode.
- Pool size is 1 by default. If you need multiple simultaneous ambient layers, the pool auto-expands.

## Music

Crossfade between tracks, pause/resume, track history. Never pauses. Pool size is 2 (enables crossfade between two tracks).

| Method | Returns | Description |
|--------|---------|-------------|
| `play_music(resource: AudioStream, crossfade: float = 0.0, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play music with optional crossfade |
| `play_music_from_position(resource: AudioStream, position: float, crossfade: float = 0.0, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play from specific position (seconds) |
| `play_music_at_volume(resource: AudioStream, volume: float, crossfade: float = 0.0, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play at specific volume (dB, not linear) |
| `play_music_from_position_at_volume(resource: AudioStream, position: float, volume: float, crossfade: float = 0.0, override_bus: String = "") -> AudioStreamPlayer` | The player used | Play from position at volume |
| `stop_music(fade_out: float = 0.0) -> void` | void | Stop all music with optional fade-out |
| `pause_music(resource: AudioStream = null) -> void` | void | Pause specific track or all music if null |
| `resume_music(resource: AudioStream = null) -> void` | void | Resume specific track or all music if null |
| `is_music_playing(resource: AudioStream = null) -> bool` | bool | Check if specific track (or any music) is playing |
| `is_music_track_playing(resource_path: String) -> bool` | bool | Check by resource path string |
| `get_currently_playing_music() -> Array` | Array of AudioStream | Currently playing music resources |
| `get_currently_playing_music_tracks() -> PackedStringArray` | PackedStringArray | Resource paths of playing tracks |
| `get_music_track_history() -> PackedStringArray` | PackedStringArray | Recent tracks, max 50, newest first |
| `get_last_played_music_track() -> String` | String | Resource path of last played track |
| `get_music_volume() -> float` | 0.0 to 1.0 | Current music volume |
| `set_music_volume(volume: float) -> void` | void | Set volume (0.0 to 1.0) |
| `set_default_music_bus(bus: String) -> void` | void | Override the auto-detected bus |

**Music crossfade mechanics:**
- When `play_music()` is called with `crossfade > 0`, the current track fades out over `crossfade * 2` seconds while the new track fades in over `crossfade` seconds.
- Fade-out easing: `TRANS_CIRC / EASE_IN` (fast initial drop, slow tail).
- Fade-in easing: `TRANS_QUAD / EASE_OUT` (smooth ramp up).
- A fade-out that reaches -80 dB or below triggers automatic stop and pool return.

**Music gotchas:**
- `play_music_at_volume()` takes volume in dB (not 0-1 linear). This is different from `set_music_volume()` which takes 0-1 linear.
- Track history is capped at 50 entries. New tracks insert at index 0.
- `pause_music(null)` pauses all playing music. `resume_music(null)` resumes all.
- `get_last_played_music_track()` does `track_history[0]` with no bounds check. If no music has ever been played, this crashes with index-out-of-bounds. Always guard with `get_music_track_history().size() > 0`.

## Process Mode Properties

Control how each subsystem behaves when the scene tree is paused.

| Property | Type | Default | Effect |
|----------|------|---------|--------|
| `sound_process_mode` | `ProcessMode` | `PROCESS_MODE_PAUSABLE` | Sound effects pause with game |
| `ui_sound_process_mode` | `ProcessMode` | `PROCESS_MODE_ALWAYS` | UI sounds always play |
| `ambient_sound_process_mode` | `ProcessMode` | `PROCESS_MODE_ALWAYS` | Ambient always plays |
| `music_process_mode` | `ProcessMode` | `PROCESS_MODE_ALWAYS` | Music always plays |

Set these on the SoundManager singleton if you need different pause behavior:
```gdscript
SoundManager.sound_process_mode = Node.PROCESS_MODE_ALWAYS  # sounds play during pause
```

## Return Value Usage

Most `play_*` methods return the `AudioStreamPlayer` used for playback. This is useful for:

```gdscript
# Connecting to the finished signal for one-shot follow-up
var player: AudioStreamPlayer = SoundManager.play_sound(explosion_sfx)
player.finished.connect(_on_explosion_finished)

# Adjusting pitch or volume on the returned player
var p: AudioStreamPlayer = SoundManager.play_sound(footstep_sfx)
p.pitch_scale = randf_range(0.9, 1.1)
```

The returned player is managed by the pool. Do not call `queue_free()` on it. Do not hold long-lived references to it, as it will be recycled after playback completes. Do not call `player.stop()` directly — pool recycling is driven by the `finished` signal, so manually stopping a player without going through SoundManager's API leaves it stuck in `busy_players` permanently (until the pool needs to reclaim it via `increase_pool()`).
