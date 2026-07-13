# Symphony Audio System — User Guide

> A complete guide to creating procedural sounds, adaptive music, and spatial audio for games using the Symphony module and its GDScript Game Audio Layer.

---

## Table of Contents

1. [Overview & Architecture](#overview--architecture)
2. [Quick Start](#quick-start)
3. [SoundEvent — The Core Resource](#soundevent--the-core-resource)
4. [AudioManager — Playing Sounds](#audiomanager--playing-sounds)
5. [DSP Graphs — Procedural Audio](#dsp-graphs--procedural-audio)
6. [Operator Reference](#operator-reference)
7. [RTPC — Real-Time Parameter Control](#rtpc--real-time-parameter-control)
8. [Music System — Adaptive Music](#music-system--adaptive-music)
9. [BeatClock — Timing & Synchronization](#beatclock--timing--synchronization)
10. [Ambient System & AudioZone2D](#ambient-system--audiozone2d)
11. [Dialogue Audio Pipeline](#dialogue-audio-pipeline)
12. [BusController — Snapshots & Ducking](#buscontroller--snapshots--ducking)
13. [Voice Management & Virtualization](#voice-management--virtualization)
14. [LOD System — Distance-Based Quality](#lod-system--distance-based-quality)
15. [Performance & Profiling](#performance--profiling)
16. [Preset Library](#preset-library)
17. [Best Practices & Tips](#best-practices--tips)
18. [Troubleshooting](#troubleshooting)

---

## Overview & Architecture

Symphony is a two-layer audio system:

```
┌─────────────────────────────────────────────────────────────┐
│  Game Audio Layer (GDScript autoloads)                       │
│  AudioManager · MusicSystem · AmbientSystem                 │
│  DialogueAudioPipeline · AudioZone2D                        │
└────────────────────────────┬────────────────────────────────┘
                             │ calls
┌────────────────────────────▼────────────────────────────────┐
│  Symphony C++ Module (modules/symphony/)                     │
│  SoundEvent · VoiceManager · BeatClock · BusController      │
│  RTPCEngine · EventDispatcher · DSP Graph Engine            │
└─────────────────────────────────────────────────────────────┘
```

**C++ Module** (`modules/symphony/`) — The low-level engine:
- Arena-allocated DSP graph compiler (zero allocation during playback)
- 36 audio operators (oscillators, filters, envelopes, delays, synthesis, spectral)
- Runtime singletons: `SymphonyVoicePool`, `SymphonyEventDispatcher`, `BeatClock`, `BusController`, `RTPCEngine`
- Micro-block processing (32/64 samples per step)

**Game Audio Layer** (`addons/symphony_audio/`) — The high-level GDScript API:
- `AudioManager` — play/stop events, volume control, RTPC forwarding
- `MusicSystem` — adaptive state machine with beat-quantized transitions
- `AmbientSystem` — zone-based ambient loops and scatter events
- `DialogueAudioPipeline` — queued voice lines with priority and ducking
- Debug overlay (F10 in debug builds)

**Key design principles:**
- All configuration lives in `.tres` resource files (text-serializable, version-control friendly)
- Lock-free communication between game thread and audio thread
- Importance-based voice stealing and virtualization (like Wwise/FMOD)
- No audio memory allocation during gameplay

---

## Quick Start

### 1. Enable the Plugin

In your Godot project, enable the `symphony_audio` plugin under Project → Project Settings → Plugins.

This registers the autoloads: `AudioManager`, `MusicSystem`, `AmbientSystem`, `DialogueAudioPipeline`.

### 2. Play a Simple Sound Effect

```gdscript
# Load a SoundEvent resource
var sfx: SoundEvent = preload("res://audio/events/explosion.tres")

# Play it (non-positional)
AudioManager.play_event(sfx)

# Play at a 2D position
AudioManager.play_event(sfx, enemy.global_position)

# Play with per-voice parameters
AudioManager.play_event(sfx, position, {"size": 2.0, "debris_amount": 0.8})
```

### 3. Play a Procedural Sound (DSP Graph)

```gdscript
# SoundEvent wraps an AudioStreamSymphony graph
var fire_event: SoundEvent = preload("res://audio/events/fire_loop.tres")
var handle: int = AudioManager.play_event(fire_event)

# Modify parameters in real-time
AudioManager.set_parameter(handle, &"intensity", 0.8)

# Stop with fade
AudioManager.stop(handle, 1.5)
```

### 4. Set Up Adaptive Music

```gdscript
# Load a music state graph
var music_graph: MusicStateGraph = preload("res://audio/music/combat_graph.tres")
MusicSystem.load_graph(music_graph)

# Transition between states
MusicSystem.set_state(&"combat")      # Crossfades on next bar
MusicSystem.set_state(&"exploration") # Transition follows graph rules
```

---

## SoundEvent — The Core Resource

`SoundEvent` is the primary resource type for playing audio. It wraps one or more audio streams (file-based or procedural graphs) with playback rules.

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `streams` | Array[AudioStream] | [] | Audio streams (WAV, OGG, or AudioStreamSymphony graphs) |
| `variation_mode` | Enum | RANDOM | How to pick from multiple streams: `RANDOM`, `SEQUENCE`, `SHUFFLE` |
| `pitch_range` | Vector2 | (1.0, 1.0) | Random pitch range (min, max) |
| `volume_range` | Vector2 | (0.0, 0.0) | Random volume offset in dB (min, max) |
| `priority` | int | 50 | Voice priority (0-100, higher = harder to steal) |
| `max_voices` | int | 0 | Max simultaneous voices (0 = unlimited) |
| `steal_mode` | Enum | OLDEST | How to steal when at max: `OLDEST`, `QUIETEST`, `FARTHEST` |
| `cooldown_ms` | float | 0.0 | Minimum ms between triggers |
| `category` | Enum | SFX | Bus category: `SFX`, `MUSIC`, `UI`, `AMBIENT`, `VOICE` |
| `bus_override` | String | "" | Override the default bus for this category |
| `importance_weight` | float | 1.0 | Multiplier for importance scoring |
| `spatial_mode` | Enum | NON_POSITIONAL | `NON_POSITIONAL`, `2D`, `3D` |
| `attenuation_model` | Enum | LINEAR | `LINEAR`, `LOGARITHMIC`, `CUSTOM` |
| `attenuation_curve` | Curve | null | Custom attenuation curve (when model = CUSTOM) |
| `max_distance` | float | 2000.0 | Maximum audible distance (pixels/units) |
| `loop` | bool | false | Whether the stream loops |
| `virtualize_when_inaudible` | bool | true | Virtualize instead of kill when inaudible |
| `rtpc_bindings` | Array[Dictionary] | [] | Parameter-to-target mappings |

### Creating a SoundEvent (.tres)

Create via the Inspector in Godot, or write directly:

```ini
[gd_resource type="SoundEvent" format=3]

[ext_resource type="AudioStream" path="res://audio/streams/footstep_01.wav" id="1"]
[ext_resource type="AudioStream" path="res://audio/streams/footstep_02.wav" id="2"]
[ext_resource type="AudioStream" path="res://audio/streams/footstep_03.wav" id="3"]

[resource]
streams = [ExtResource("1"), ExtResource("2"), ExtResource("3")]
variation_mode = 2
pitch_range = Vector2(0.9, 1.1)
volume_range = Vector2(-2.0, 2.0)
cooldown_ms = 80.0
category = 0
spatial_mode = 1
max_distance = 800.0
```

### Cooldown & Voice Limit Sharing

Cooldown and voice limits are keyed by the SoundEvent **resource instance**:
- All scripts that `load()` or `preload()` the same `.tres` file share the same cooldown timer and voice count (like Wwise/FMOD global event limiting).
- If you need **per-emitter** limits (e.g., each enemy has independent footstep cooldown), call `event.duplicate()` to create a separate instance.
- `SoundEvent.new()` always creates a unique instance that does NOT share cooldown with file-loaded events.
- Variation sequence/shuffle state is also per-instance.

---

## AudioManager — Playing Sounds

`AudioManager` is the primary GDScript API for all sound playback. It's an autoload singleton.

### Core API

```gdscript
# Play a sound. Returns a voice handle (int) or -1 if rejected.
var handle: int = AudioManager.play_event(event, position, params)

# Stop a voice
AudioManager.stop(handle)              # Immediate
AudioManager.stop(handle, 0.5)         # Fade out over 0.5s

# Stop all voices of an event
AudioManager.stop_by_event(event, 1.0)

# Stop all voices in a category
AudioManager.stop_by_category(0, 1.0)  # 0 = SFX

# Stop everything
AudioManager.stop_all(2.0)

# Check if a voice is still playing
if AudioManager.is_playing(handle):
    pass
```

### Per-Voice Parameters

```gdscript
# Set a parameter on a specific voice
AudioManager.set_parameter(handle, &"intensity", 0.7)

# Set a global parameter (affects all voices with matching RTPC bindings)
AudioManager.set_global_parameter(&"tension", 0.9)

# Read a global parameter's current (smoothed) value
var tension: float = AudioManager.get_global_parameter(&"tension")
```

### Category Volume Control

```gdscript
# Categories: 0=SFX, 1=Music, 2=UI, 3=Ambient, 4=Voice

# Set category volume (0.0 - 1.0 linear)
AudioManager.set_category_volume(1, 0.5)          # Music at 50%
AudioManager.set_category_volume(1, 0.5, 1.0)     # Fade to 50% over 1s

# Mute/unmute
AudioManager.set_category_muted(0, true)           # Mute SFX

# Read current volume
var vol: float = AudioManager.get_category_volume(1)
```

### Volume Stack

Each voice has a 4-layer volume stack that combines additively in dB:

| Layer | Purpose | Updated by |
|-------|---------|-----------|
| [0] initial_db | Random volume from SoundEvent.volume_range | Set once at play |
| [1] rtpc_db | RTPC-driven volume offset | Per-frame RTPC evaluation |
| [2] attenuation_db | Distance attenuation | Per-frame from VoiceManager |
| [3] fade_db | Fade-in/fade-out | Manual fade via `stop(handle, time)` |

Final volume = sum of all 4 layers, clamped to [-80, +24] dB.

### Bus Layout

The default bus layout:
- **Master** → SFX, Music, UI, Ambient, Voice

Each `SoundEvent.category` maps to a bus name automatically. Use `bus_override` to route to a custom bus.

---

## DSP Graphs — Procedural Audio

Symphony's DSP graph engine lets you create sounds entirely from code — no audio files needed. Graphs are saved as `AudioStreamSymphony` resources (`.tres` files).

### How It Works

1. You define a graph of connected **operators** (nodes)
2. At play time, the graph is **compiled** into an arena-allocated execution plan
3. The audio thread runs the graph per micro-block (32-64 samples) with zero allocation
4. Parameters can be modulated in real-time from GDScript via `GraphInput` nodes

### Graph Structure

Every graph needs:
- At least one **source** (Oscillator, Noise, WavePlayer, etc.)
- A **GraphOutput** node (the final output to speakers)
- Optionally: **GraphInput** nodes for real-time parameter control
- Optionally: **TriggerInput** nodes for one-shot envelope triggers

### Example: Simple Laser Sound

```
[Oscillator] → [ADSR] → [SVFilter] → [GraphOutput]
      ↑            ↑
[GraphInput]  [TriggerInput]
 "frequency"    "fire"
```

The `.tres` representation:

```ini
[gd_resource type="AudioStreamSymphony" format=3]

[resource]
mix_rate = 48000.0
voice_priority = 50

graph/node_count = 5
graph/nodes/0/id = 0
graph/nodes/0/type = &"TriggerInput"
graph/nodes/0/params = {"name": "fire"}
graph/nodes/1/id = 1
graph/nodes/1/type = &"GraphInput"
graph/nodes/1/params = {"name": "frequency", "default_value": 880.0}
graph/nodes/2/id = 2
graph/nodes/2/type = &"Oscillator"
graph/nodes/2/params = {"waveform": 1}
graph/nodes/3/id = 3
graph/nodes/3/type = &"ADSR"
graph/nodes/3/params = {"attack_ms": 1.0, "decay_ms": 100.0, "sustain": 0.0, "release_ms": 50.0}
graph/nodes/4/id = 4
graph/nodes/4/type = &"GraphOutput"
graph/nodes/4/params = {}

graph/connection_count = 4
graph/connections/0/from_node = 1
graph/connections/0/from_pin = 0
graph/connections/0/to_node = 2
graph/connections/0/to_pin = 1
graph/connections/1/from_node = 0
graph/connections/1/from_pin = 0
graph/connections/1/to_node = 3
graph/connections/1/to_pin = 1
graph/connections/2/from_node = 2
graph/connections/2/from_pin = 0
graph/connections/2/to_node = 3
graph/connections/2/to_pin = 0
graph/connections/3/from_node = 3
graph/connections/3/from_pin = 0
graph/connections/3/to_node = 4
graph/connections/3/to_pin = 0
```

### Playing Graph Sounds from GDScript

```gdscript
# Wrap the graph in a SoundEvent for full voice management
var laser_event: SoundEvent = preload("res://audio/events/laser.tres")

# Play and get a handle
var handle: int = AudioManager.play_event(laser_event, position, {"frequency": 1200.0})

# Trigger the envelope (required for TriggerInput-based graphs!)
# Access the underlying playback:
var player: Node = AudioManager._slot_to_player[handle]
var playback = player.get_stream_playback()
playback.trigger(&"fire")

# Modulate parameters in real-time
AudioManager.set_parameter(handle, &"frequency", 440.0)
```

### TriggerInput — One-Shot Sounds

Graphs with `TriggerInput` nodes require an explicit `playback.trigger(&"name")` call after `play()`. Without it, the graph runs but ADSR/envelope never fires — you'll hear silence.

### GraphOutput — Combining Sources

`GraphOutput` has a single input pin. To mix multiple sources, use `MathAdd` or `Mix` nodes before `GraphOutput`:

```
[Noise] ──→ [Mix] ──→ [GraphOutput]
[Oscillator] ─↗
```

### SubGraph — Reusable Modules

Use `SubGraph` operators to embed one graph inside another. This enables modular design — build reusable "building blocks" (e.g., a reverb send, a common filter chain) and reference them.

---

## Operator Reference

Symphony provides 36 DSP operators organized by category:

### Generators (sound sources)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **Oscillator** | Classic waveforms (sine, saw, square, triangle) with PolyBLEP anti-aliasing | `waveform`, `frequency`, `phase` |
| **Noise** | White, pink, brown noise | `mode` (0=white, 1=pink, 2=brown) |
| **LFO** | Low-frequency oscillator for modulation | `frequency`, `waveform`, `amplitude` |
| **Constant** | Outputs a fixed value | `value` |
| **WavePlayer** | Plays back a PCM audio buffer | `buffer`, `playback_rate`, `loop` |
| **FMOscillator** | 2-operator FM synthesis | `carrier_freq`, `mod_freq`, `mod_index` |
| **FormantOsc** | Vowel/formant synthesis | `vowel`, `frequency`, `formant_shift` |

### Filters (shape the spectrum)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **BiquadFilter** | Standard biquad (LP, HP, BP, Notch, Peak, LowShelf, HighShelf) | `mode`, `frequency`, `q` |
| **OnePole** | Simple 1-pole LP/HP (6dB/oct) | `mode`, `cutoff` |
| **SVFilter** | State-variable filter (LP, HP, BP, Notch simultaneously) | `cutoff`, `resonance` |
| **DCBlocker** | Removes DC offset | — |
| **Saturator** | Soft-clip saturation/distortion | `drive` |
| **Waveshaper** | Arbitrary transfer function distortion | `curve`, `drive`, `mix` |

### Envelopes & Dynamics

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **ADSR** | Attack-Decay-Sustain-Release envelope | `attack_ms`, `decay_ms`, `sustain`, `release_ms` |
| **Gain** | Simple volume control | `gain` |
| **Compressor** | Dynamic range compression | `threshold`, `ratio`, `attack_ms`, `release_ms` |

### Math & Mixing

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **MathAdd** | Add two signals | — |
| **Mix** | Weighted blend of two signals | `mix` (0.0 = A only, 1.0 = B only) |
| **CrossFade** | Crossfade between two inputs via control signal | `control` |
| **MapRange** | Remap a value from one range to another | `in_min`, `in_max`, `out_min`, `out_max` |
| **SampleHold** | Samples input on trigger, holds value | — |
| **RingMod** | Ring modulation (multiply two signals) | — |

### Timing & Triggers

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **Clock** | Periodic trigger signal | `bpm`, `subdivision` |
| **TriggerDelay** | Delays a trigger by a fixed time | `delay_ms` |
| **StochasticTrigger** | Random trigger with controllable density | `density` (events/sec) |

### Delay & Reverb

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **DelayLine** | Simple delay with feedback | `delay_ms`, `feedback` |
| **FeedbackPath** | Explicit feedback routing for complex topologies | `delay_samples` |
| **PitchShifter** | Granular pitch shifting | `semitones`, `grain_size_ms` |
| **FDNReverb** | Feedback Delay Network reverb | `room_size`, `decay`, `damping`, `mix` |

### Utility

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **ParameterSmoother** | Smooths a control signal (prevents clicks) | `smooth_time_ms` |
| **EnvelopeFollower** | Extracts amplitude envelope from audio | `attack_ms`, `release_ms` |

### Synthesis

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **ModalBank** | Resonant modal synthesis (bells, metals, glass) | `frequencies[]`, `decays[]`, `gains[]` |
| **GrainCloud** | Granular synthesis engine | `grain_size_ms`, `density`, `position`, `pitch_randomness`, `scan_speed`, `amp_randomness`, `pitch_tracking` |

### Spectral

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **PhaseVocoder** | FFT-based time-stretch and pitch-shift | `time_stretch`, `pitch_shift` |
| **SpectralGate** | Frequency-domain noise gate | `threshold`, `ratio` |

### I/O (Graph Interface)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| **GraphInput** | Exposes a named parameter to GDScript | `name`, `default_value` |
| **GraphInputAudio** | Routes external audio into the graph | `channel` |
| **GraphOutput** | Final output to speakers (one per graph) | — |
| **TriggerInput** | Named trigger for one-shot events | `name` |
| **SubGraph** | Embeds another graph as a node | `graph_resource` |

---

## RTPC — Real-Time Parameter Control

RTPC (Real-Time Parameter Control) connects game state to audio parameters with automatic smoothing.

### Global Parameters

```gdscript
# Register a parameter (do this at startup)
RTPCEngine.register_global_parameter("player_health", 1.0, 10.0)  # name, default, smooth_ms
RTPCEngine.register_global_parameter("tension", 0.0, 50.0)
RTPCEngine.register_global_parameter("speed", 0.0, 20.0)

# Update from gameplay code
AudioManager.set_global_parameter(&"player_health", player.health / player.max_health)
AudioManager.set_global_parameter(&"tension", combat_intensity)

# Read current smoothed value
var health: float = AudioManager.get_global_parameter(&"player_health")
```

### RTPC Bindings in SoundEvent

RTPC bindings map a parameter to a target property with an optional curve:

```ini
# In a SoundEvent .tres file
rtpc_bindings = [{
    "parameter_name": &"speed",
    "target": 0,           # 0=Pitch, 1=Volume, 2=FilterCutoff, 3=GraphInput, 4=PlaybackSpeed
    "curve": null,         # Optional Curve resource for non-linear mapping
    "min_value": 0.0,
    "max_value": 100.0,
    "graph_input_name": &""  # Only used when target = 3 (GraphInput)
}]
```

### RTPC Targets

| Target ID | Name | Effect |
|-----------|------|--------|
| 0 | PITCH | Modulates `pitch_scale` on the player |
| 1 | VOLUME | Modulates volume via the volume stack (layer [1]) |
| 2 | FILTER_CUTOFF | Reserved for per-voice filter (future) |
| 3 | GRAPH_INPUT | Routes mapped value to a named `GraphInput` in the DSP graph |
| 4 | PLAYBACK_SPEED | Same as pitch (affects speed + pitch together) |

### Per-Voice vs Global Parameters

- **Global**: Set with `AudioManager.set_global_parameter()`. Affects all voices with matching bindings.
- **Per-voice**: Set with `AudioManager.set_parameter(handle, name, value)`. Overrides global for that voice only.

Per-voice parameters are resolved first. If none is set, the global value is used.

### Parameter Smoothing

All global parameters are smoothed on the audio thread using a one-pole filter. The `smooth_time_ms` parameter controls how fast the value converges to the target. Use short times (5-10ms) to prevent clicks, longer times (50-200ms) for gradual transitions.

---

## Music System — Adaptive Music

`MusicSystem` is a state-machine-based adaptive music engine with beat-quantized transitions and layer control.

### Music State Graph

Define your music structure as a `MusicStateGraph` resource:

```gdscript
# Each state has:
# - A name (StringName)
# - A stream (AudioStream) or layers (Array of streams)
# - BPM and time signature
# - Transition rules to other states
```

States can be:
- **Single-stream** — one audio file per state (simple)
- **Layered** — multiple synchronized streams that can be individually enabled/disabled

### Basic Usage

```gdscript
# Load and start
var graph: MusicStateGraph = preload("res://audio/music/level_music.tres")
MusicSystem.load_graph(graph)

# The initial state plays automatically

# Transition to a different state
MusicSystem.set_state(&"combat")

# Transitions respect the graph's rules (quantization, crossfade type, duration)
```

### Transition Types

| Type | Description |
|------|-------------|
| `CROSSFADE` | Smooth crossfade between states |
| `FADE_THROUGH_SILENCE` | Fade out → silence → fade in |
| `CUT` | Immediate switch (no fade) |
| `STINGER` | Play a transition sound, then switch |

### Quantization

Transitions can be quantized to musical boundaries:

| Quantization | Behavior |
|--------------|----------|
| `IMMEDIATE` | Transition starts now |
| `NEXT_BEAT` | Waits for the next beat |
| `NEXT_BAR` | Waits for the next bar (downbeat) |

```gdscript
# The graph defines default quantization per transition rule.
# Example: combat→exploration always waits for next bar.
MusicSystem.set_state(&"exploration")  # Will wait for next bar automatically
```

### Layer Control

For layered states, toggle individual layers:

```gdscript
# Check available layers
var layers: PackedStringArray = MusicSystem.get_layer_names()
# e.g., ["drums", "bass", "melody", "strings"]

# Enable/disable layers with fade
MusicSystem.set_layer_active(&"drums", true, 0.5)     # Fade in over 0.5s
MusicSystem.set_layer_active(&"strings", false, 2.0)  # Fade out over 2s

# Check layer state
if MusicSystem.is_layer_active(&"melody"):
    pass
```

### Signals

```gdscript
MusicSystem.state_changed.connect(_on_music_state_changed)
MusicSystem.transition_started.connect(_on_transition_started)
MusicSystem.transition_completed.connect(_on_transition_completed)
MusicSystem.beat_hit.connect(_on_beat)
MusicSystem.bar_hit.connect(_on_bar)

func _on_music_state_changed(from: StringName, to: StringName) -> void:
    print("Music: %s → %s" % [from, to])

func _on_beat(beat_index: int) -> void:
    # Sync visual effects to the beat
    flash_indicator()
```

### Stopping Music

```gdscript
MusicSystem.stop()        # Immediate stop
MusicSystem.stop(2.0)     # Fade out over 2 seconds
```

---

## BeatClock — Timing & Synchronization

`BeatClock` is a C++ singleton that provides latency-compensated beat tracking. It's automatically driven by `MusicSystem` but can also be used standalone.

### Query API

```gdscript
# Current position
var beat: float = BeatClock.get_current_beat()       # e.g., 2.75 (beat 3, 75% through)
var bar: int = BeatClock.get_current_bar()           # e.g., 4
var fraction: float = BeatClock.get_beat_fraction()  # 0.0 - 1.0 within current beat

# Time until next boundary
var to_beat: float = BeatClock.get_time_to_next_beat()  # seconds
var to_bar: float = BeatClock.get_time_to_next_bar()    # seconds

# Configuration
var bpm: float = BeatClock.get_bpm()
var beats_per_bar: int = BeatClock.get_beats_per_bar()
var playing: bool = BeatClock.is_playing()
```

### Music Moment Alignment (Time-Stretch)

Align musical events to gameplay moments:

```gdscript
# "Boss door opens in 3.2 seconds — make the next downbeat hit at that moment"
var stretch: float = MusicSystem.align_beat_to_time(3.2)
# Returns the stretch ratio applied (1.0 = no change needed)
```

### Time-Stretch Conventions

Symphony uses two conventions — be careful which API you use:

| API | Convention | >1.0 means |
|-----|-----------|------------|
| `BeatClock.calculate_time_stretch_for_alignment()` | Rate-based | Faster |
| `BeatClock.calculate_duration_stretch_for_alignment()` | Duration-based | Slower |
| `PhaseVocoder` time_stretch input | Duration-based | Slower |
| `AudioStreamPlayer.pitch_scale` | Rate-based | Faster |

**Rule of thumb:**
- Use `calculate_time_stretch_for_alignment()` for `pitch_scale` or playback rate
- Use `calculate_duration_stretch_for_alignment()` for PhaseVocoder graphs

```gdscript
# Rate-based (for AudioStreamPlayer):
player.pitch_scale = BeatClock.calculate_time_stretch_for_alignment(target_time)

# Duration-based (for PhaseVocoder graph input):
playback.set_parameter("time_stretch", BeatClock.calculate_duration_stretch_for_alignment(target_time))
```

---

## Ambient System & AudioZone2D

The ambient system creates immersive environments with zone-based audio, crossfading, and scatter events.

### AudioZone2D Node

`AudioZone2D` extends `Area2D`. Add it to your scene with a `CollisionShape2D` child to define the zone boundary.

**Properties:**

```
Ambient Loop:
  loop_event: SoundEvent           — Primary ambient loop
  loop_volume: float (0-1)         — Base volume for the loop
  loop_event_variants: Dictionary  — {StringName: SoundEvent} for day/night switching

Zone Influence:
  influence_radius: float          — Distance beyond boundary where zone still has influence
  fade_time: float                 — Crossfade duration when entering/leaving (seconds)
  zone_priority: int (0-100)       — Higher wins when zones overlap
  fade_curve_type: Enum            — LINEAR, SMOOTHSTEP, INVERSE_DISTANCE, CUSTOM

Scatter:
  scatter_events: Array[SoundEvent] — Pool of one-shot events to scatter
  scatter_rate: float               — Events per second (average)
  scatter_radius: float             — Max distance from listener for scatter placement
  scatter_min_distance: float       — Min distance from listener
```

### Setting Up an Ambient Zone

1. Add an `AudioZone2D` node to your scene
2. Add a `CollisionShape2D` child (CircleShape2D, RectangleShape2D, etc.)
3. Assign a looping `SoundEvent` to `loop_event`
4. Optionally add scatter events for random one-shots

```gdscript
# The zone auto-registers with AmbientSystem on _ready()
# No code needed for basic functionality!
```

### Zone Influence & Crossfading

- **Inside zone**: Full volume (influence = 1.0)
- **Within influence_radius**: Fades based on `fade_curve_type`
- **Beyond influence_radius**: Silent (influence = 0.0)

Multiple zones crossfade smoothly as the listener moves. `AmbientSystem.max_simultaneous_zones` limits concurrent active zones (default: 3).

### Day/Night Variants

```gdscript
# Define variants on the AudioZone2D:
# loop_event_variants = {"day": day_forest_event, "night": night_forest_event}

# Switch all active zones simultaneously:
AmbientSystem.set_variant(&"night")  # Crossfades all zones to night variant
AmbientSystem.set_variant(&"day")    # Back to day
```

### Per-Zone Dynamic Changes

```gdscript
# Change a single zone's loop at runtime (e.g., after a gameplay event):
zone.set_loop_event(new_event)
# Crossfades using the zone's fade_time property.
# If zone is not active, update is silent — takes effect on next activation.
```

### Scatter Events

Scatter events create ambient detail (bird chirps, dripping water, rustling leaves):

```gdscript
# Configured on AudioZone2D:
# scatter_events = [bird_chirp_event, wing_flutter_event]
# scatter_rate = 3.0    # ~3 events per second
# scatter_radius = 600  # Within 600px of listener
# scatter_min_distance = 80  # Not too close

# Scatter fires automatically while the zone is active.
# Position is randomized within the zone boundary near the listener.
```

### Custom Listener

```gdscript
# Override automatic listener detection:
AmbientSystem.set_listener_node(my_character)

# Return to auto-detection (uses Camera2D/Camera3D):
AmbientSystem.set_listener_node(null)
```

---

## Dialogue Audio Pipeline

`DialogueAudioPipeline` manages voice line playback with queueing, priority interruption, and automatic ducking.

### Basic Usage

```gdscript
# Play a voice line
var stream: AudioStream = preload("res://audio/voice/npc_greeting.ogg")
var player: AudioStreamPlayer = DialogueAudioPipeline.play_voice_line(stream, &"merchant", 50)

# Check if something is playing
if DialogueAudioPipeline.is_line_playing():
    print("Speaker: ", DialogueAudioPipeline.get_current_speaker())

# Stop current line
DialogueAudioPipeline.stop_current_line(0.1)  # 100ms fade

# Queue multiple lines
DialogueAudioPipeline.queue_line(line_1, &"narrator", 50)
DialogueAudioPipeline.queue_line(line_2, &"narrator", 50)
```

### Interruption Modes

```gdscript
# Configure how new lines interact with playing lines
DialogueAudioPipeline.set_interruption_mode(DialogueAudioPipeline.InterruptionMode.PRIORITY)
```

| Mode | Behavior |
|------|----------|
| `PRIORITY` | Higher priority interrupts lower. Equal/lower is queued. |
| `QUEUE` | All lines queue sequentially regardless of priority. |
| `REJECT` | New line is rejected if something is already playing. |
| `DUCK_AND_OVERLAY` | New line plays simultaneously, existing ducks slightly. |

### Auto-Ducking

When a voice line plays, configured categories are ducked automatically:

```gdscript
# Configure ducking (defaults duck Music by -6dB)
DialogueAudioPipeline.set_duck_targets(
    [1],      # Categories to duck (1 = Music)
    -6.0,     # Duck amount in dB
    100.0,    # Attack ms
    500.0     # Release ms
)
```

Ducking engages via signal (instant on play/stop) — this is "precision ducking." `BusController` provides separate poll-based "safety net" ducking.

### Dialogue Manager Integration

If using the Dialogue Manager addon, integrate via tags:

```gdscript
# In your dialogue handler:
func _on_dialogue_line(line: DialogueLine) -> void:
    DialogueAudioPipeline.play_from_dialogue_line(line, _advance_dialogue)
```

Tag your dialogue lines with `[voice:res://audio/voice/line_01.ogg]` and optionally `[priority:70]`.

### Signals

```gdscript
DialogueAudioPipeline.line_started.connect(func(speaker): print(speaker, " speaking"))
DialogueAudioPipeline.line_finished.connect(func(speaker): print(speaker, " done"))
```

---

## BusController — Snapshots & Ducking

`BusController` is a C++ singleton that manages bus volume snapshots and automatic ducking.

### Snapshots

Capture the current bus state and recall it later:

```gdscript
# Capture current state
BusController.capture_snapshot(&"default")
BusController.capture_snapshot(&"combat")

# After modifying volumes for combat...
BusController.capture_snapshot(&"combat")

# Apply a snapshot (with transition time)
BusController.apply_snapshot(&"default", 2.0)  # Crossfade over 2 seconds
BusController.apply_snapshot(&"combat", 0.5)   # Quick transition

# Query
var names: PackedStringArray = BusController.get_snapshot_names()
if BusController.has_snapshot(&"exploration"):
    BusController.apply_snapshot(&"exploration", 1.0)
```

**Important:** `apply_snapshot()` resets ducking state. After a snapshot applies, auto-ducking re-attacks from 0 dB. You may hear a brief volume swell on ducked buses.

### Auto-Ducking

BusController monitors a source bus and ducks target buses when audio is present:

```gdscript
# Configure (defaults: Voice bus ducks Music + Ambient)
BusController.set_duck_source_bus(&"Voice")
BusController.set_duck_target_buses([&"Music", &"Ambient"])
BusController.set_duck_amount_db(-6.0)
BusController.set_duck_attack_ms(100.0)
BusController.set_duck_release_ms(500.0)
BusController.set_duck_silence_threshold_db(-60.0)

# Query
if BusController.is_ducking_active():
    var duck_db: float = BusController.get_current_duck_db()
```

### Volume Stacking Note

Ducking and `set_category_volume()` both modify the same `AudioServer` bus volume. Their effects stack additively. If auto-ducking applies -6 dB and category volume is set to -3 dB, the bus will be at -9 dB. This is intentional — there are no separate VCA lanes.

If you need independent volume control, use separate Godot audio buses.

---

## Voice Management & Virtualization

Symphony manages a fixed pool of voice slots (default: 48) with importance-based stealing and virtualization.

### How Voice Stealing Works

When the pool is full and a new sound plays:
1. The event's `steal_mode` determines the victim: `OLDEST`, `QUIETEST`, or `FARTHEST`
2. The stolen voice fades out over ~64 samples (anti-click)
3. The new voice takes the slot

Higher `priority` voices are harder to steal. `importance_weight` further modifies the scoring.

### Importance Calculation

Each voice's importance is computed from:
- **Priority** (from SoundEvent)
- **Distance** (closer = more important)
- **Category weight** (Voice > UI > SFX = Music > Ambient)
- **Importance weight** (per-event multiplier)

Importance is updated in a staggered fashion (1/4 of the pool per frame) to spread CPU cost.

### Virtualization

When a voice becomes inaudible (very far away or volume ≈ 0):
- It's **virtualized** — the player stops, releasing DSP resources
- The slot remains reserved — the voice "remembers" it's playing
- When the listener moves closer, it **devirtualizes** — playback resumes

```gdscript
# Connect to virtualization signals
SymphonyVoicePool.voice_virtualized.connect(_on_voice_virtualized)
SymphonyVoicePool.voice_devirtualized.connect(_on_voice_devirtualized)

func _on_voice_virtualized(slot: int) -> void:
    # Release expensive resources if needed
    pass

func _on_voice_devirtualized(slot: int) -> void:
    # First ~50ms may be silent for live-input graphs (masked by fade-in)
    pass
```

### Per-Voice Parameters on the Pool

```gdscript
# Local parameters override global RTPC for a specific voice
SymphonyVoicePool.set_local_parameter(slot, &"damage", 1.0)
var val: float = SymphonyVoicePool.get_local_parameter(slot, &"damage")
var has: bool = SymphonyVoicePool.has_local_parameter(slot, &"damage")
```

---

## LOD System — Distance-Based Quality

The LOD (Level of Detail) system switches between graph complexity levels based on listener distance.

### How It Works

Each `AudioStreamSymphony` can define up to 3 graph variants:
- **LOD 0** — Full quality (close to listener)
- **LOD 1** — Simplified (medium distance)
- **LOD 2** — Minimal (far away, e.g., Noise → Filter → Gain)

### Default Thresholds

| LOD | Distance Ratio | Description |
|-----|---------------|-------------|
| 0 | 0% - 30% of max_distance | Full graph |
| 1 | 30% - 70% of max_distance | Simplified |
| 2 | 70% - 100% of max_distance | Minimal |

Transitions use 5% hysteresis to prevent flip-flopping at boundaries.

### LOD Crossfade

When switching LOD levels, both graphs run in parallel for ~2048 samples (~42ms) to crossfade smoothly.

### Per-Slot Threshold Override

```gdscript
# After acquiring a slot, override the default thresholds:
SymphonyVoicePool.set_slot_lod_thresholds(slot, 0.2, 0.6)
# LOD 0→1 at 20% distance, LOD 1→2 at 60% distance
```

Thresholds reset on each `acquire_slot()`.

### Manual LOD Control

```gdscript
# Force a specific LOD (disables auto-LOD for this slot)
SymphonyVoicePool.force_lod(slot, 2)  # Force minimal quality

# Re-enable automatic LOD
SymphonyVoicePool.release_lod_force(slot)

# Query current LOD
var current: int = SymphonyVoicePool.get_slot_current_lod(slot)
var target: int = SymphonyVoicePool.get_slot_target_lod(slot)
```

### Best Practice: LOD Graph Design

- LOD 0: Full synthesis chain (oscillators, reverb, grain clouds, etc.)
- LOD 1: Remove reverb, reduce oscillator count, simplify filters
- LOD 2: Replace synthesis with `Noise → Filter → Gain` (minimal CPU)
- **Never use GrainCloud in LOD 2** — replace with filtered noise

---

## Performance & Profiling

### Performance Monitors

Symphony registers custom Godot performance monitors (visible in Debugger → Monitors):

| Monitor | Description |
|---------|-------------|
| `audio/active_voices` | Currently playing voice count |
| `audio/virtual_voices` | Virtualized (sleeping) voice count |
| `audio/stolen_this_frame` | Voice steals this frame |
| `audio/budget_percent` | Pool utilization (0-100%) |
| `audio/voices_sfx` | Active SFX voices |
| `audio/voices_music` | Active music voices |
| `audio/voices_ambient` | Active ambient voices |
| `audio/voices_ui` | Active UI voices |
| `audio/voices_voice` | Active dialogue voices |
| `audio/events_per_second` | Event fire rate |
| `audio/steals_per_second` | Voice steal rate |
| `audio/virtualizations_per_second` | Virtualization rate |
| `audio/music_beat` | Current beat position |
| `audio/music_bar` | Current bar number |

### Debug Overlay

Press **F10** in debug builds to toggle the audio debug overlay. It shows:
- Real-time voice pool state
- Bus levels
- Active event log
- Category voice counts
- BeatClock position

### Programmatic Stats

```gdscript
var stats: Dictionary = AudioManager.get_debug_stats()
# {"active": 12, "virtual": 3, "stolen": 0, "budget_percent": 25.0}

var active: int = AudioManager.get_active_voice_count()
var virtual: int = AudioManager.get_virtual_voice_count()
```

### Memory Budget Guidelines

| Component | Memory per Voice |
|-----------|-----------------|
| Standard graph (10-15 nodes) | ~4-8 KB |
| GrainCloud (live-input) | ~384 KB (capture buffer) |
| GrainCloud (PCM source) | Shared via cache key |
| FDNReverb | ~16 KB (delay lines) |
| PhaseVocoder | ~32 KB (FFT buffers) |

### CPU Budget Guidelines

- Target: < 5% of frame budget on audio
- Stagger heavy operations (importance update already staggers 1/4 per frame)
- Use LOD aggressively for distant voices
- Max 48 simultaneous voices (pool size) — typically 20-30 active is healthy

### Testing Utilities

```gdscript
# Force immediate importance update (for tests only, NOT production)
SymphonyVoicePool.update_importance_all()

# In production, importance updates are staggered (1/4 pool per frame)
```

---

## Preset Library

Symphony ships with 90+ ready-to-use procedural graph presets. Use them directly or as starting points for customization.

### Available Presets

**Impacts & Combat:**
`explosion`, `metal_impact`, `wood_impact`, `stone_impact`, `glass_impact`, `soft_impact`, `sword_clash`, `shield_impact`, `impact_punch`, `laser_zap`, `laser_rifle`

**Nature & Environment:**
`fire`, `torch_fire`, `wind`, `wind_howl`, `wind_gust`, `rain`, `thunder`, `ocean_waves`, `flowing_water`, `waterfall`, `cave_drip`, `lava_bubble`, `ice_crack`, `ambient_forest`

**Creatures & Voice:**
`creature_growl`, `creature_chirp`, `hiss_snarl`, `insect_buzz`, `vocalization`, `breath_exhale`

**Mechanical & Industrial:**
`engine_idle`, `engine_rev`, `electric_motor_large`, `servo_motor`, `mechanical_gear`, `hydraulic_press`, `propeller_fan`, `chain_rattle`, `door_creak`, `steam_hiss`, `friction_squeak`

**UI & Feedback:**
`ui_click`, `ui_notification`, `ui_sweep`, `coin_collect`, `power_up`

**Music & Tonal:**
`drum_kick`, `drum_snare`, `bell_chime`, `plucked_string`, `bowed_string`, `acid_bass`, `distortion_guitar`

**Sci-Fi & Magic:**
`teleport`, `magic_shimmer`, `magic_charge`, `spellcast_release`, `crystal_resonance`, `portal_hum`, `energy_beam`, `warp_drive`, `sci_fi_scanner`, `alien_warble`, `digital_glitch`, `sonar_ping`, `electricity_arc`

**Movement & Physics:**
`footstep_dirt`, `footstep_stone`, `whoosh_swing`, `bouncing`, `rolling`, `sliding_scraping`, `arrow_projectile`, `cloth_flap`, `rope_chain`, `spring_twang`, `tire_surface`, `crumbling`, `dust_debris`, `rumble_earthquake`

**Continuous & Loops:**
`drone_pad`, `heartbeat`, `electric_hum`, `radio_static`, `underwater`, `tube_resonance`, `warning_klaxon`, `alarm_siren`, `bubble_drip`

### Using Presets

```gdscript
# Load a preset graph directly
var fire_stream: AudioStreamSymphony = preload(
    "res://addons/symphony_audio/presets/graphs/fire.tres"
)

# Wrap in a SoundEvent for full voice management
var fire_event := SoundEvent.new()
fire_event.streams = [fire_stream]
fire_event.category = SoundEvent.CATEGORY_AMBIENT
fire_event.spatial_mode = SoundEvent.SPATIAL_2D
fire_event.loop = true
fire_event.max_distance = 600.0

# Play
var handle: int = AudioManager.play_event(fire_event, torch_position)

# Modulate the intensity parameter
AudioManager.set_parameter(handle, &"intensity", 0.8)
```

### Preset Parameters

Most presets expose a `GraphInput` parameter for real-time control. Common ones:
- `intensity` — overall amount/strength (fire, wind, rain, engine)
- `size` — scale of the effect (explosion, impact)
- `frequency` — pitch/fundamental (oscillator-based sounds)
- `speed` — rate of modulation (engine_rev, rolling)
- `debris_amount` — amount of secondary detail (explosion, crumbling)

---

## Best Practices & Tips

### GrainCloud Usage

- Default capture buffer is 2 seconds. Set `capture_seconds` explicitly for longer buffers (up to 10s desktop, 4s web).
- For PCM source presets: always pass `source_pcm_cache_key` (resource path) to enable memory sharing across voices.
- **Never use GrainCloud in LOD 2 graphs** — replace with `Noise → Filter → Gain`.
- Live-input granulation allocates 384KB per voice — budget accordingly.

### Graph Design

- Keep graphs small for frequently-played sounds (5-10 nodes for footsteps/UI)
- Use `ParameterSmoother` on `GraphInput` values to prevent clicks
- Use `MathAdd` or `Mix` to combine multiple sources before `GraphOutput`
- Use `SubGraph` for reusable processing chains (reverb sends, common filters)

### Event Design

- Use `cooldown_ms` to prevent machine-gun repetition (80-150ms for footsteps)
- Use `pitch_range` and `volume_range` for natural variation
- Set `max_voices` for sounds that shouldn't stack (e.g., max_voices=1 for UI clicks)
- Use `SHUFFLE` variation mode for sequential variety without immediate repetition

### Spatial Audio

- Set `spatial_mode = 2D` (or 3D) for any positioned sound
- Symphony handles distance attenuation via the volume stack — Godot's built-in attenuation is disabled
- Use `LOGARITHMIC` attenuation for realistic falloff, `LINEAR` for predictable control
- Use `CUSTOM` + a Curve resource for precise artistic control

### Music Design

- Keep music layers pre-rendered as WAV/OGG (not procedural graphs)
- Use `NEXT_BAR` quantization for musical transitions
- Use BeatClock signals for gameplay synchronization (rhythmic effects, animations)
- Design stingers as short one-shots that play during transitions

### Resource Management

- Operator names in `.tres` files are StringNames — never rename operators without registering an alias
- Use `OperatorRegistry::register_alias("OldName", "NewName")` for backward compatibility
- All callers sharing a `.tres` file share the same variation sequence — use `duplicate()` for independent instances

### Performance

- Voice pool default is 48 slots. 20-30 active voices is healthy; above 40 investigate
- Use LOD graphs for all spatial sounds (especially ambient)
- Monitor `audio/budget_percent` — target < 60%
- Steals/virtualizations per second should be < 5 in normal gameplay
- GrainCloud and PhaseVocoder are expensive — use sparingly, never in LOD 2

---

## Troubleshooting

### Sound doesn't play

1. **Returns -1 from play_event?**
   - Check `cooldown_ms` — event may be in cooldown
   - Check `max_voices` — voice limit may be reached
   - Check that `streams` array is not empty in the SoundEvent

2. **Graph plays but no audio heard?**
   - If using `TriggerInput`, you must call `playback.trigger(&"name")` after play
   - Check that all nodes connect to `GraphOutput`
   - Verify the graph has at least one sound source (Oscillator, Noise, WavePlayer, etc.)

3. **Bus is muted or volume is 0?**
   - Check `AudioManager.get_category_volume(category)`
   - Check if `BusController` ducking is active
   - Check `AudioServer.is_bus_mute(bus_idx)`

### Sound cuts out unexpectedly

1. **Voice stolen** — another higher-priority sound took the slot
   - Increase `priority` or `importance_weight` on the SoundEvent
   - Check `audio/steals_per_second` monitor

2. **Voice virtualized** — listener moved too far away
   - Increase `max_distance` on the SoundEvent
   - Set `virtualize_when_inaudible = false` to kill instead of virtualize

3. **Player pool exhausted** — all 16 players are busy
   - This shouldn't happen with 48 voice slots and 16 players. Check for leaked handles.

### Crackling or clicks

1. **Parameter changes without smoothing** — use `ParameterSmoother` in graphs, or set adequate `smooth_time_ms` on RTPC parameters
2. **Voice stealing** — anti-click fade is 64 samples (~1.3ms). If you hear clicks on steals, report as bug.
3. **Buffer underrun** — check CPU usage. Reduce active voice count or graph complexity.

### Music transition sounds wrong

1. **Transition not waiting for bar?** — check that the transition rule has `quantization: NEXT_BAR`
2. **BPM mismatch after transition?** — each state should define its own `bpm` and `beats_per_bar`
3. **Layers not synced?** — ensure all layer streams are exactly the same length

### Ambient zone issues

1. **Zone never activates?**
   - Check that `AudioZone2D` has a `CollisionShape2D` child
   - Check that `loop_event` is assigned
   - Verify `AmbientSystem` autoload is running

2. **Scatter events never fire?**
   - Check `scatter_rate > 0` and `scatter_events` is not empty
   - Ensure zone's collision_mask includes its own collision_layer
   - Check that listener is within zone influence

3. **Zones flip-flop rapidly?**
   - This is prevented by hysteresis (5%). If still happening, increase `fade_time` or adjust `zone_priority` to disambiguate.

### RTPC not affecting sound

1. Verify the parameter is registered: `RTPCEngine.has_global_parameter("name")`
2. Check that `SoundEvent.rtpc_bindings` has a binding for the parameter
3. For `GraphInput` targets, verify `graph_input_name` matches the `GraphInput` node's `name` in the graph
4. Per-voice parameters override global — check if one is set

### Debug Tools

- **F10** — Toggle debug overlay (bus meters, voice list, event log)
- **Performance monitors** — Debugger → Monitors → audio/*
- `AudioManager.get_debug_stats()` — programmatic stats
- Run with `--verbose` flag for additional BusController/ducking logs
- `SymphonyVoicePool.get_recent_events(20)` — recent event log with timestamps

---

## API Quick Reference

### Singletons (C++ — available everywhere)

| Singleton | Purpose |
|-----------|---------|
| `SymphonyVoicePool` | Voice pool management, importance, LOD |
| `SymphonyEventDispatcher` | Event cooldown/variation/voice limit |
| `BeatClock` | Beat tracking and time-stretch |
| `BusController` | Snapshot and ducking management |
| `RTPCEngine` | Global parameter registry and smoothing |

### Autoloads (GDScript)

| Autoload | Purpose |
|----------|---------|
| `AudioManager` | Primary play/stop API, volume control |
| `MusicSystem` | Adaptive music state machine |
| `AmbientSystem` | Zone-based ambient audio |
| `DialogueAudioPipeline` | Voice line queue and ducking |

### Common Patterns

```gdscript
# One-shot SFX
AudioManager.play_event(sfx_event, position)

# Looping ambient attached to an object
var h: int = AudioManager.play_event(loop_event, object.global_position)
# Update position each frame if object moves:
SymphonyVoicePool.set_slot_position(h, Vector3(object.global_position.x, object.global_position.y, 0.0))

# Reactive sound (engine RPM)
var engine_handle: int = AudioManager.play_event(engine_event)
func _process(_delta):
    AudioManager.set_parameter(engine_handle, &"rpm", current_rpm / max_rpm)

# Music state change on game event
func _on_enemy_spotted():
    MusicSystem.set_state(&"combat")

func _on_enemies_cleared():
    MusicSystem.set_state(&"exploration")

# Settings menu volume
func _on_music_slider_changed(value: float):
    AudioManager.set_category_volume(1, value, 0.3)  # Music, fade 0.3s
```

---

*Symphony Audio System v1.0 — Procedural sound, adaptive music, and spatial audio for Godot.*
