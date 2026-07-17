# Dev Log — Symphony Audio System

## User Guide Topics (to document)

The following topics need proper user-facing documentation before the module is used by others. Currently they exist only as dev-log notes or source comments.

### GrainCloud Best Practices
- Default capture buffer is 2s. Set `capture_seconds` explicitly if you need more (up to 10s desktop, 4s web).
- For source_pcm presets: always pass `source_pcm_cache_key` (resource path) to enable memory sharing across voices.
- LOD tier 2 graphs should NOT use GrainCloud. Replace with Noise→Filter→Gain for distant voices.
- Live-input granulation allocates 384KB per voice — budget accordingly.

### GrainCloud Pin Evolution (S4.3)
- GrainCloud grew from 5 to 8 input pins in S4.3: added `scan_speed`, `amp_randomness`, `pitch_tracking`.
- Old `.tres` files with only 5 connections still load correctly — unconnected pins default to `nullptr` and parameter defaults are used (scan_speed=0, amp_randomness=0, pitch_tracking=0).
- No migration action required. New pins are all `required = false`.
- Pin layout: `audio_in`(0), `grain_size_ms`(1), `density`(2), `position`(3), `pitch_randomness`(4), `scan_speed`(5), `amp_randomness`(6), `pitch_tracking`(7).

### Virtualization & Memory
- Connect to `SymphonyVoicePool.voice_virtualized` / `voice_devirtualized` signals.
- On virtualize: call `player.stop()` to release graph memory.
- On devirtualize: call `player.play()` to recompile. First ~50ms may be silent for live-input mode (masked by fade-in).

### PhaseVocoder Time-Stretch
- `time_stretch = 2.0` = output is 2× longer (half-speed). Matches DAW convention.
- If using BeatClock's `calculate_time_stretch_for_alignment()` output, invert with `1.0 / stretch`.
- Preferred: use `calculate_duration_stretch_for_alignment()` directly — it returns the reciprocal.

### Time-Stretch Convention: BeatClock vs PhaseVocoder

Symphony uses two conventions for time-stretch values:

| API | Convention | >1.0 means |
|-----|-----------|------------|
| `BeatClock.calculate_time_stretch_for_alignment()` | Rate-based | Faster (compressed duration) |
| `BeatClock.calculate_duration_stretch_for_alignment()` | Duration-based | Slower (longer output) |
| `PhaseVocoder` time_stretch input | Duration-based | Slower (longer output) |
| `AudioStreamPlayer.pitch_scale` | Rate-based | Faster/higher pitch |

**Rule of thumb:**
- Use `calculate_time_stretch_for_alignment()` when setting `pitch_scale` or playback rate.
- Use `calculate_duration_stretch_for_alignment()` when feeding a PhaseVocoder node.

Example (GDScript):
```gdscript
# Align next bar to a gameplay event 3.2 seconds from now
var target_time = 3.2

# For AudioStreamPlayer (rate-based):
player.pitch_scale = BeatClock.calculate_time_stretch_for_alignment(target_time)

# For PhaseVocoder graph input (duration-based):
playback.set_parameter("time_stretch", BeatClock.calculate_duration_stretch_for_alignment(target_time))
```

### TriggerInput One-Shots
- Graphs with TriggerInput nodes require explicit `playback.trigger(&"name")` call after `play()`.
- Without it, the graph runs but the ADSR/envelope never fires.

### BusController Snapshots & Ducking
- Ducking effects stack with `set_category_volume()`. Both modify the same AudioServer bus volume. This is intentional — there are no separate volume lanes (VCAs). If you need independent control, use separate buses.
- `apply_snapshot()` resets ducking state automatically. After a snapshot applies, auto-ducking will re-attack from 0 dB over the configured `attack_ms`. This prevents offset drift but means you may hear a brief volume swell on ducked buses immediately after a snapshot transition.
- A one-time verbose log is emitted when ducking first activates to remind about stacking behavior. Visible with `--verbose` flag.

### LOD System
- 3 tiers: LOD 0 (full), LOD 1 (simplified), LOD 2 (minimal).
- Default thresholds: 30% / 70% of max_distance with 5% hysteresis.
- Per-slot thresholds: call `VoicePool.set_slot_lod_thresholds(slot, threshold_1, threshold_2)` after `acquire_slot()` to override defaults. Read values from `AudioStreamSymphony.lod_threshold_1` / `lod_threshold_2`.
- If not explicitly set, slots use 0.3 / 0.7 defaults. Thresholds reset on each `acquire_slot()`.
- LOD crossfade: 2048 samples (~42ms) parallel execution of both graphs.
- `force_lod()` / `release_lod_force()` for manual control.

### GraphOutput & Multi-Source Mixing
- GraphOutput has a single input pin. Use Mix/MathAdd nodes to combine multiple sources before output.

### Operator Naming & .tres Compatibility
- .tres files reference operators by StringName. Never rename operators without registering an alias.
- Alias system: call `OperatorRegistry::register_alias("OldName", "NewName")` in `register_types.cpp` to maintain backward compatibility with existing .tres files.
- When a deprecated name is resolved via alias, a `WARN_PRINT` is emitted once per compile prompting the user to update their resource files.
- Aliases are a migration bridge, not permanent infrastructure. Remove them after a major version bump once all .tres files are updated.

### AudioZone2D Loop Event Hot-Swap
- Call `zone.set_loop_event(new_event)` at runtime to change the ambient loop with a crossfade.
- The crossfade uses the zone's `fade_time` property — old loop fades out, new loop starts at current volume.
- If the zone is not currently active (listener is out of range), the property updates silently and takes effect on next activation.
- For time-of-day or weather-based switching across multiple zones, prefer `AmbientSystem.set_variant()` — it crossfades all active zones simultaneously.
- `set_loop_event()` is for per-zone dynamic changes (e.g., a zone that changes based on gameplay state).

### SoundEvent Cooldown & Voice Limit Sharing
- Cooldown and voice limits are keyed by the SoundEvent's **resource instance**, not by caller.
- All scripts that `load()` or `preload()` the same `.tres` file share the same cooldown timer and voice count. This is intentional — it mirrors Wwise/FMOD global event limiting.
- If you need **per-emitter** cooldown/voice limits (e.g., each enemy has independent footstep cooldown), call `event.duplicate()` to create a separate instance.
- `SoundEvent.new()` always creates a unique instance — it will NOT share cooldown with file-loaded events of the same configuration.
- Variation sequence/shuffle state is also per-instance. All callers sharing a `.tres` advance the same sequence counter.

### Testing: update_importance_all()
- `SymphonyVoicePool.update_importance_all()` forces all voice slots to recompute importance scores immediately.
- Use this in tests instead of calling `process_frame()` 4+ times. The normal `update_importance()` is staggered (processes 1/4 of the pool per frame) to spread CPU cost.
- **Do not use in production gameplay code** — the staggered approach prevents per-frame spikes with many active voices.

---

## Completed Phases

| Phase | Scope | Status |
|-------|-------|--------|
| S1 | Core Completion — DelayLine, FeedbackPath, SVFilter, ParameterSmoother, StochasticTrigger, PolyBLEP | ✅ |
| S2 | Synthesis Toolkit — ModalBank, GrainCloud, PitchShifter, Waveshaper, FM, CrossFade | ✅ |
| S3 | Advanced Synthesis — FormantOsc, FDN Reverb, Waveguide presets, LOD graphs | ✅ |
| S4 | Spectral — PhaseVocoder, SpectralGate, enhanced GrainCloud | ✅ |
| G1 | Foundation — SoundEvent, VoiceManager, EventDispatcher, AudioManager autoload | ✅ |
| G2 | Music System — MusicStateGraph, BeatClock, layer control | ✅ |
| G3 | RTPC Integration — parameter smoothing, curve evaluation | ✅ |
| G4 | Dialogue & Bus — DialogueAudioPipeline, BusController snapshots, auto-ducking | ✅ |
| G5 | Ambient & Spatial — AudioZone2D, scatter layers, distance attenuation | ✅ |
| G6 | Profiling & Debug — performance monitors, debug overlay | ✅ |
| M1 | Music Quality — TransitionAnalyzer, Musical Coherence Validation | ✅ |

---

### M1: Transition Quality & Musical Coherence

**Motivation:** Based on Luo & Reiss (AES 2025) Procedural Music Evaluation metrics — Feasibility (transition quality) and Coherence (overall musical consistency). Applied to improve the Music System's transition smoothness and validate LLM-generated graph resources.

#### TransitionAnalyzer (C++ Singleton)
- **File:** `runtime/transition_analyzer.h/.cpp`
- Renders short analysis windows from outgoing tail + incoming head using PFFFT
- Computes RMS (loudness continuity) and spectral centroid (brightness continuity)
- Returns feasibility score (0.0-1.0) and curve recommendation
- 4 curve types: `LINEAR`, `EQUAL_POWER`, `S_CURVE`, `FADE_SILENCE`
- Temporary 16KB allocation, freed immediately. Zero per-frame cost.
- Registered as singleton, accessible from GDScript

#### Musical Coherence Validation (MusicStateGraph extension)
- **File:** `runtime/music_state_graph.h/.cpp`
- Optional metadata on states: `key`, `energy`, `spectral_centroid_hz`
- `validate_musical_coherence()` → Array of diagnostic dictionaries
- Checks: energy delta, tempo ratio, key distance (circle of fifths), centroid ratio
- Relative major/minor exemption (C↔Am = no penalty)
- `coherence_override` flag per transition to suppress warnings
- Configurable thresholds exposed as Resource properties
- Zero runtime cost — only evaluated at load time or on explicit call

#### MusicSystem Integration
- `load_graph()` auto-runs `validate_musical_coherence()`, prints warnings
- `_execute_crossfade()` calls `TransitionAnalyzer.analyze_transition()` for curve selection
- Auto-switches to `FADE_THROUGH_SILENCE` when analyzer recommends it
- `_crossfade_in_player()` applies `LINEAR`/`EQUAL_POWER`/`S_CURVE` via Tween ease types
- `transition_analyzed` signal emitted with full analysis dict
- `auto_curve_selection` flag (global + per-transition opt-out)
- `get_last_analysis_result()` public API

## Operator Registry (36 total)

**Generators**: Oscillator, Constant, Noise, LFO, WavePlayer, FMOscillator, FormantOsc
**Filters**: BiquadFilter, OnePole, DCBlocker, Saturator, SVFilter, Waveshaper
**Envelopes**: Gain, ADSR, Compressor
**Math**: MathAdd, Mix, MapRange, SampleHold, RingMod, CrossFade
**Timing**: Clock, TriggerDelay, StochasticTrigger
**Delay**: DelayLine, FeedbackPath, PitchShifter, FDNReverb
**Utility**: ParameterSmoother, EnvelopeFollower
**Synthesis**: ModalBank, GrainCloud
**Spectral**: PhaseVocoder, SpectralGate
**I/O**: GraphInput, GraphInputAudio, GraphOutput, TriggerInput, SubGraph
