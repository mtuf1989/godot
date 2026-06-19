# Setup and Patterns

Installation, audio bus configuration, and common usage patterns for the Sound Manager addon.

## Table of Contents

- [Installation](#installation)
- [Audio Bus Layout](#audio-bus-layout)
- [Bus Auto-Detection](#bus-auto-detection)
- [Common Patterns](#common-patterns)
- [AudioStream Resource Setup](#audiostream-resource-setup)
- [Shared Bus Warning](#shared-bus-warning)
- [Pool Behaviour](#pool-behaviour)
- [Gotchas and Edge Cases](#gotchas-and-edge-cases)

## Installation

1. Copy `addons/sound_manager/` into the project's `addons/` directory.
2. Enable the plugin in Project > Project Settings > Plugins.
3. The plugin registers `SoundManager` as an autoload singleton automatically via `plugin.gd`.
4. Verify: `SoundManager` should be listed in Project Settings > Autoload.

If the autoload is missing after enabling the plugin, add it manually:
- Path: `res://addons/sound_manager/sound_manager.gd`
- Name: `SoundManager`

## Audio Bus Layout

The plugin expects a bus layout with separate buses for sounds and music. The default layout shipped with the addon:

```
Master
  +-- Sounds    (for sound effects, UI sounds, ambient sounds)
  +-- Music     (for music playback)
```

To create this in Godot:
1. Open the Audio tab at the bottom of the editor.
2. Click "Add Bus" twice.
3. Name them "Sounds" and "Music".
4. Both should route to Master (default).
5. Save as `default_bus_layout.tres` (Godot does this automatically).

The `.tres` file looks like:
```
[gd_resource type="AudioBusLayout" format=3]

[resource]
bus/1/name = &"Sounds"
bus/1/solo = false
bus/1/mute = false
bus/1/bypass_fx = false
bus/1/volume_db = 0.0
bus/1/send = &"Master"
bus/2/name = &"Music"
bus/2/solo = false
bus/2/mute = false
bus/2/bypass_fx = false
bus/2/volume_db = 0.0
bus/2/send = &"Master"
```

## Bus Auto-Detection

Each subsystem tries candidate bus names against AudioServer in this order:

| Subsystem | Candidates tried |
|-----------|-----------------|
| Sound Effects | Sounds, SFX |
| UI Sounds | UI, Interface, Sounds, SFX |
| Ambient Sounds | Sounds, SFX |
| Music | Music |

For each candidate, the plugin tries five case variations:
1. Original (e.g., "Sounds")
2. lowercase (e.g., "sounds")
3. camelCase (e.g., "sounds")
4. PascalCase (e.g., "Sounds")
5. snake_case (e.g., "sounds")

If no candidate matches, the subsystem falls back to "Master".

To override auto-detection, call `set_default_*_bus()` at startup:
```gdscript
func _ready() -> void:
    SoundManager.set_default_sound_bus("GameSFX")
    SoundManager.set_default_music_bus("BGM")
```

## Common Patterns

### Basic sound effect on hit
```gdscript
const HIT_SFX: AudioStream = preload("res://assets/audio/sfx/hit.wav")

func take_damage(amount: int) -> void:
    health -= amount
    SoundManager.play_sound(HIT_SFX)
```

### UI button click
```gdscript
const CLICK_SFX: AudioStream = preload("res://assets/audio/ui/click.wav")

func _on_button_pressed() -> void:
    SoundManager.play_ui_sound(CLICK_SFX)
```

### Randomized pitch for variety
```gdscript
const FOOTSTEP: AudioStream = preload("res://assets/audio/sfx/footstep.wav")

func _play_footstep() -> void:
    var player: AudioStreamPlayer = SoundManager.play_sound(FOOTSTEP)
    player.pitch_scale = randf_range(0.9, 1.1)
```

### Music with crossfade on scene change
```gdscript
const MENU_MUSIC: AudioStream = preload("res://assets/audio/music/menu.ogg")
const GAME_MUSIC: AudioStream = preload("res://assets/audio/music/game.ogg")

func _start_game() -> void:
    SoundManager.play_music(GAME_MUSIC, 2.0)  # 2s crossfade from menu music

func _return_to_menu() -> void:
    SoundManager.play_music(MENU_MUSIC, 1.5)
```

### Ambient sound lifecycle
```gdscript
const RAIN: AudioStream = preload("res://assets/audio/ambient/rain.ogg")
const WIND: AudioStream = preload("res://assets/audio/ambient/wind.ogg")

func _enter_outdoor_area() -> void:
    SoundManager.play_ambient_sound(RAIN, 1.0)   # 1s fade-in
    SoundManager.play_ambient_sound(WIND, 0.5)

func _enter_indoor_area() -> void:
    SoundManager.stop_ambient_sound(RAIN, 2.0)    # 2s fade-out
    SoundManager.stop_ambient_sound(WIND, 1.5)
```

### Volume settings UI
```gdscript
func _on_music_slider_changed(value: float) -> void:
    SoundManager.set_music_volume(value)  # 0.0 to 1.0

func _on_sfx_slider_changed(value: float) -> void:
    SoundManager.set_sound_volume(value)
    SoundManager.set_ui_sound_volume(value)  # often tied together
    SoundManager.set_ambient_sound_volume(value)

func _load_volume_settings() -> void:
    music_slider.value = SoundManager.get_music_volume()
    sfx_slider.value = SoundManager.get_sound_volume()
```

### Pause menu audio
Sound effects pause with the game by default. UI sounds and music continue. If you need sound effects to also play during pause:
```gdscript
SoundManager.sound_process_mode = Node.PROCESS_MODE_ALWAYS
```

### Stop all music with fade
```gdscript
func _on_game_over() -> void:
    SoundManager.stop_music(3.0)  # 3s fade-out
```

### Check what music is playing
```gdscript
func _is_battle_music_on() -> bool:
    return SoundManager.is_music_track_playing("res://assets/audio/music/battle.ogg")
```

## AudioStream Resource Setup

### Looping
The Sound Manager does not control looping. Loop settings live on the AudioStream resource:
- `.wav` files: Set loop mode in the Import dock (Loop Mode: Forward).
- `.ogg` files: Set `loop = true` in the Import dock or in the `.import` file.
- `.mp3` files: Set `loop = true` in the Import dock.

Ambient sounds and music tracks that should loop continuously must have looping enabled on the resource itself.

### AudioStreamRandomizer
The plugin has special handling for `AudioStreamRandomizer` resources. When `prepare()` encounters one, it reuses an existing player with that resource rather than allocating a new one. This means you can use `AudioStreamRandomizer` for varied sound effects (footsteps, hits) and the pool handles it efficiently.

### Supported formats
Any format Godot supports works: `.wav`, `.ogg`, `.mp3`, `.opus`. For sound effects, `.wav` is typical (low latency). For music and ambient, `.ogg` is common (smaller file size, good quality).

## Shared Bus Warning

The plugin emits `push_warning` at runtime if:
- Multiple subsystems share the Master bus (meaning bus auto-detection failed to find dedicated buses).
- Music and sound effects share the same bus.

This warning means volume controls will affect multiple subsystems simultaneously. Fix by adding the expected buses to `default_bus_layout.tres`.

## Pool Behaviour

| Subsystem | Default Pool Size | Behaviour |
|-----------|------------------|-----------|
| Sound Effects | 8 | 8 simultaneous sounds before expansion |
| UI Sounds | 8 | 8 simultaneous UI sounds before expansion |
| Ambient Sounds | 1 | Auto-expands for multiple ambient layers |
| Music | 2 | Enables crossfade (one fading out, one fading in) |

When a pool is exhausted:
1. The pool first tries to reclaim idle busy players (players that finished but were not yet returned).
2. If none available, it creates a new AudioStreamPlayer node (auto-expansion).
3. When players are returned to the pool and the pool exceeds `default_pool_size`, excess players are freed.

You do not need to manage pool sizes manually. The defaults handle typical game audio loads.

## Gotchas and Edge Cases

1. **Volume units differ by method.** `set_*_volume()` takes 0.0-1.0 linear. `play_music_at_volume()` takes dB. Do not mix them up.

2. **Ambient duplicate prevention.** `play_ambient_sound(resource)` silently does nothing if that exact resource is already playing. This is by design. Do not treat it as a bug.

3. **Crossfade fade-out is 2x.** When `play_music(track, crossfade=2.0)`, the old track fades out over 4 seconds (2x the crossfade value), while the new track fades in over 2 seconds.

4. **Do not queue_free returned players.** The AudioStreamPlayer returned by `play_*` methods belongs to the pool. Freeing it will break the pool.

5. **AudioStreamRandomizer reuse.** If you pass an `AudioStreamRandomizer` to `play_sound()`, the pool reuses an existing player with that resource rather than allocating a new one. This is efficient but means the returned player may already be mid-playback of a previous random variant.

6. **C# wrapper.** If the project uses C#, the `NathanHoad.SoundManager` static class wraps the GDScript singleton. All logic runs in GDScript. The C# API uses PascalCase (`PlaySound`, `PlayMusic`, etc.) and delegates via `Engine.GetSingleton("SoundManager")`.

7. **No built-in audio ducking.** The plugin does not duck sound effects when music plays or vice versa. If you need ducking, implement it via AudioServer bus effects (Compressor with sidechain) or manual volume adjustments.

8. **Track history cap.** `get_music_track_history()` returns at most 50 entries, newest first. Older entries are silently dropped.

9. **Bus auto-detection runs only at `_init()` time.** The bus is resolved once during initialization. If you add audio buses dynamically after the game starts, SoundManager won't detect them. Use `set_default_*_bus()` to manually assign dynamic buses.

10. **Pool reclamation can steal players before playback starts.** When the pool is exhausted, `increase_pool()` reclaims busy players whose `playing` property is `false`. Because playback is started via `call_deferred("play")`, a player marked busy may not yet be playing on the same frame. In extremely rapid fire-and-forget scenarios, a sound could be reclaimed before it starts.

11. **Manual `player.stop()` leaks players to busy_players.** Pool recycling is driven by the `finished` signal. If you manually stop a returned player without going through SoundManager's `stop_sound()`/`stop_music()` API, the player stays in `busy_players` permanently until the pool needs to reclaim it via `increase_pool()`.

12. **`stop_sound()` uses reference equality, not path.** `sound_effects.stop()` compares `player.stream == resource` (object identity). `preload()` in different scripts returns the same object, but `load()` at runtime or duplicating the resource creates a different object that `stop_sound()` won't match.
