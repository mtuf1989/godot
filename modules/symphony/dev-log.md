# Dev Log — Symphony Audio System

## Music N3 (2026-10-09)

Recorded phrases: vendored Signalsmith Stretch 1.3.0 + Linear 0.1.2 (plan said 1.3.2; tag not published). `SymphonyMusicStereoCache` (interleaved stereo float, budget-charged). `SymphonyStretchContext`/`Pool` — presetDefault + splitComputation, seek/prime on main, process on audio, Returning reclaim. Tempo/transpose envelope clamps; common-latency hybrid synth+clip render. Tests: `test_symphony_music_n3.cpp`. Suite: 294/294.

## Music N2 (2026-10-09)

`MusicNoteInput` hub (freq/vel/pitch_ratio/triggers/gate); `runtime/symphony_note_pool.h` — resettable graph validation, seeded `reset_for_note` voices, release deadlines, protected-track capacity. Scheduler-driven multi-track phrase render; 44.1k/48k tone-level equivalence. Tests: `test_symphony_music_n2.cpp`. Suite: 289/289. `AudioStreamSymphonyMusic` wiring deferred to G2.

## P10 (2026-10-09)

Instrument readiness for music N2: `OperatorDescriptor.resettable`, pristine arena blobs, `reset_for_note` (import → clear_large_buffers → reseed via `symphony_node_seed`). Noise export/reseed; Biquad full-state export for bit-exact reset; ModalBank base tables + `pitch_ratio`/`root_hz` + Nyquist mute; migrate scratch >256. Tests: `test_symphony_operator_reset.cpp`. game-template: 11 ModalBank presets drop PitchShifter; operator-schema synced. Suite: 283/283.

## P7 (2026-10-09)

SFX timing leftovers after N1: `SymphonyClock` fires its first tick at sample 0, takes FLOAT `bpm` (block-smoothed, phase-continuous) and TRIGGER `reset`, outputs AUDIO `phase`. RTPCEngine gains asymmetric `attack_ms`/`release_ms` via `set_parameter_smoothing`. MusicStateGraph.validate errors on missing/non-positive bpm or beats_per_bar; music_system.gd no longer defaults to 120/4. Tests: `test_symphony_p7_timing.cpp`. Suite: 275/275.

## P6 (2026-10-09)

Spatial reverb rework: header-only `dsp/symphony_fdn_core.h` + `dsp/symphony_shoebox_er.h`; FDNReverb/EarlyReflections wrap the cores (fractional taps, ER A/B crossfade, always-write pre-delay). New `AudioEffectSymphonyReverb` (SeqLock params, ER+FDN, `process_silence=false`). Godot core: `AudioStreamPlayer3D.direct_volume_db` for DRR (send path excludes it). Reverb pool SlotParams → decay/absorption_high/hf_ratio/volume/dims/er_reflection/critical_distance; `reverb_level.h` DRR amount. GDScript: wet buses use Symphony reverb (`dry=0`,`wet=1`), proxy after apparent position, `get_emitter_reverb_amount` (no portal double-apply), volume stack split. FDN Mix presets retuned ~+6 dB. Tests: fdn_core, reverb_effect, early relative, spatial pool rewrite, ASP3D bind. Suite: 270/270.

## Music N1 (2026-10-09)

Transport: `runtime/symphony_music_transport.h` (960 PPQN, tempo step/ramp, meter, tick↔frame). Scheduler: `runtime/symphony_music_scheduler.h` (bounded phrase handoff, NoteOff-before-NoteOn, SeqLock snapshot). BeatClock: incremental `beat_acc`, loop-wrap, multi-beat hitch (cap 8), `fire_ahead_s`, `beat_hit`/`bar_hit` carry `target_time`. P7 R: `TriggerInput`/`trigger_at`/`get_frames_rendered` sample-accurate. `AudioStreamPlaybackInteractive::get_playback_position` returns current clip position. Tests: `test_symphony_music_n1.cpp`. Suite: 250/250. Stream class `AudioStreamSymphonyMusic` deferred to N2 wiring.

## P2 (2026-10-09)

N2 (game-template): 3D player pool sets `attenuation_filter_db=0` and `max_db=24`; gdUnit asserts both. N3: WavePlayer `decode_to_pcm16` widens 8-bit and mixes IMA ADPCM/QOA via `instantiate_playback()->mix` at 1:1 stream rate; `ERR_PRINT_ONCE` on failure. SpatialGraphWrapper wraps any pathed WAV. Tests: `test_symphony_p2_wave.cpp`.

## P4 (2026-10-09)

Sample-rate policy D: `core/symphony_rate_policy.h` (`symphony_select_music_rate`, `symphony_offline_rate`); SFX `AudioStreamSymphony::mix_rate` defaults to 48000 and clamps to [8000, 192000]. WavePlayer uses loop-aware 4-point Catmull-Rom and prefers `AudioStreamWAV::load_from_file` for on-disk WAVs. OnePole/SVFilter bypass when cutoff ≥ 0.45·sr or the 20 kHz spatial “open” sentinel. Oscillator clamps `dt` to [0, 0.49] with DC hold at 0. Tests: `test_symphony_p4_rate.cpp`. Suite: 237/237.

## P3 (2026-10-09)

Nonlinear / anti-alias DSP: Saturator uses a correct HIIR polyphase 2× half-band and u-domain ADAA (eps 1e-3, midpoint fallback; drive modulation no longer spikes). Waveshaper presets share immutable f/F tables; ADAA is three-way (same cell / adjacent / quadratic F); FOLDBACK is a continuous triangle fold. FMOscillator drops the 18 kHz pole and Carson-clamps internal `mod_index`. Anti-alias staircase LPF cutoff is `min(18000, 0.4·sr)`. FormantOsc blends `cos(nθ)`/`cos((n+1)θ)`. DCBlocker `R` tracks 15 Hz; Biquad smooths AUDIO cutoff (τ=5 ms, coeff refresh every 16 samples). Tests: `test_symphony_p3_dsp.cpp` (11 cases). Suite: 230/230.

## S0 closed (2026-10-09)

TSan after P1b: 218 Symphony cases green, 1 warning (= P0 `DynamicRangeHigh` baseline; new Symphony-frame races = 0). Lifetime T3 added. game-template: fifty `torch_fire` play/stop asserts manager voice count and packages; `finite_oneshot_test` uses graph-rate frames from `render_offline`.

## P1b (2026-10-09)

Float→Audio promotions copy after the producer (graph inputs at the top of the block). SeqLock stores atomic words and lives in `core/`. Each playback has one voice seed, reused for LOD recompiles; `render_offline` uses the render seed. Playback position is `frames_rendered / graph_rate`. Offline renders call the unresampled graph-rate mix.

## User Guide Topics (to document)

The following topics need proper user-facing documentation before the module is used by others. Currently they exist only as dev-log notes or source comments.

### GrainCloud Best Practices
- Default capture buffer is 2s. Set `capture_seconds` explicitly if you need more (up to 10s desktop, 4s web).
- For source_pcm presets: always pass `source_pcm_cache_key` (resource path) to enable memory sharing across voices.
- LOD tier 2 graphs should NOT use GrainCloud. Replace with Noise→Filter→Gain for distant voices.
- Live-input granulation allocates 384KB per voice — budget accordingly.

### GrainCloud Pin Evolution (P5)
- Pins: `audio_in`(0), `trigger`(1), `grain_size_ms`(2), `density`(3), `position`(4), `pitch_range_st`(5), `scan_speed`(6), `amp_randomness`(7), `reverse_prob`(8).
- Removed `pitch_tracking`. Legacy param `pitch_randomness` still accepted at create and maps to `pitch_range_st`.
- Max 32 grains; windows hann/tukey/expodec/rexpodec; `overlap_normalize` default on.
- RT metric: `get_dropped_grains_count()`.

### Virtualization & Memory
- Connect to `SymphonyVoicePool.voice_virtualized` / `voice_devirtualized` signals.
- On virtualize: call `player.stop()` to release graph memory.
- On devirtualize: call `player.play()` to recompile. First ~50ms may be silent for live-input mode (masked by fade-in).

### PhaseVocoder (P5)
- Pitch-shift only — `time_stretch` removed. Use `pitch_scale` / sampler rate for time-scale.
- Strongest-source phase remap; LOD `export_state` size 0.

### Time-Stretch Convention: BeatClock

| API | Convention | >1.0 means |
|-----|-----------|------------|
| `BeatClock.calculate_time_stretch_for_alignment()` | Rate-based | Faster (compressed duration) |
| `BeatClock.calculate_duration_stretch_for_alignment()` | Duration-based | Slower (longer output) |
| `AudioStreamPlayer.pitch_scale` | Rate-based | Faster/higher pitch |

```gdscript
player.pitch_scale = BeatClock.calculate_time_stretch_for_alignment(target_time)
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
