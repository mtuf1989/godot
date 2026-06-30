# Dev Log — Symphony Audio System

## 2026-06-30 — G3 RTPC & Symphony Integration

### Completed

| Task | Status | Files |
|------|--------|-------|
| G3.1 — RTPCEngine C++ | ✅ Done | `runtime/rtpc_engine.h/.cpp` |
| G3.2 — RTPCBinding on SoundEvent | ✅ Done | `runtime/sound_event.h/.cpp` |
| G3.3 — Parameter Application Pipeline | ✅ Done | `runtime/voice_manager.h/.cpp`, `audio_manager.gd` |
| G3.4 — Global and Per-Handle Parameter API | ✅ Done | `audio_manager.gd` |
| G3.5 — TransientDetector utility | ✅ Done | `addons/symphony_audio/transient_detector.gd` |
| G3.6 — Unified Playback (File + Procedural) | ✅ Done | `audio_manager.gd` |
| G3.7 — Integration Test (Footsteps) | ✅ Done | `test/audio/test_footsteps.*`, `audio/events/sfx/footstep.tres`, `audio/graphs/presets/footstep_procedural.tres` |

### Architecture Decisions

1. **RTPCEngine uses AudioServer mix callback** — true audio-thread smoothing at sample rate. One-pole coefficient per parameter. Smooths once per mix cycle (512 frames typically).

2. **Per-voice local parameters**: fixed `float[8]` array in VoiceSlot (no allocation, cache-friendly). Per-voice overrides global when both set.

3. **Unified playback via AudioStreamPlayer**: Both `AudioStream` (file) and `AudioStreamSymphony` (procedural) go through the same player pool. Godot's `instantiate_playback()` handles the difference transparently. RTPC targets `Pitch`/`Volume`/`PlaybackSpeed` use player properties; `GraphInput` uses `playback.set_parameter()`.

4. **Parameter application at 60Hz (game thread)**: The per-frame RTPC pipeline reads already-smoothed values from RTPCEngine (audio-thread smoothed) and applies to player properties. For Symphony graphs, the ParameterSmoother operator inside the graph provides the final audio-rate interpolation.

5. **Auto-register on first use**: `RTPCEngine.set_target()` auto-registers unknown parameters with default 5ms smoothing. Game code doesn't need to pre-declare parameters.

### Gotchas / Notes for Future Sessions

1. **TriggerInput for one-shots**: The footstep graph uses TriggerInput "pluck" to fire the ADSR. Game code must call `playback.trigger(&"pluck")` after `play()` — the graph won't produce sound without it. Consider adding auto-trigger support to SoundEvent.

2. **FilterCutoff RTPC target (target=2)**: Currently a no-op in the pipeline. Implementing per-voice filter requires either a dedicated AudioEffect on a per-voice bus or using Godot's `AudioStreamPlayer.attenuation_filter_cutoff_hz` (3D only). For Symphony voices, use `GraphInput` target instead.

3. **evaluate_curve with null Curve**: Falls back to linear (input=output). This is correct but may surprise users expecting a specific mapping. Document that null curve = linear pass-through.

4. **Thread safety of set_target()**: Currently writes `target_value` on the game thread while the audio thread reads `current_value` and writes `current_value`. Since `target_value` and `current_value` are separate floats and the operation is a single float store, this is practically safe on ARM64 (naturally aligned stores are atomic). But it's not formally correct — consider adding `std::atomic<float>` for `target_value` if issues arise.

5. **RTPC binding serialization**: Uses `TypedArray<Dictionary>` for .tres compatibility. Each dict has keys: `parameter_name` (StringName), `target` (int), `curve` (Curve or null), `min_value` (float), `max_value` (float), `graph_input_name` (StringName). Agent-authorable.

---

## 2026-06-30 — G2.3, G2.4, G2.5 Implementation

### Completed

| Task | Status | Files |
|------|--------|-------|
| G2.3 — MusicSystem GDScript Autoload | ✅ Done | `addons/symphony_audio/music_system.gd` |
| G2.4 — AudioStreamInteractive Integration | ✅ Done | (integrated in music_system.gd) |
| G2.5 — Music System Test Scene | ✅ Done | `test/audio/test_music_system.gd`, `.tscn` |
| Plugin registration | ✅ Done | `plugin.gd`, `project.godot` |

### What Was Built

- **MusicSystem autoload** (~580 lines) with:
  - `load_graph()` / `set_state()` / `stop()` public API
  - 4 transition types: Crossfade, FadeThroughSilence, Cut, Stinger
  - Beat-quantized transitions (NEXT_BEAT, NEXT_BAR) via BeatClock signals
  - Layer system via `AudioStreamSynchronized` with per-layer fade
  - `AudioStreamInteractive` auto-detection for simple graphs (no layers, no stingers)
  - Signals: `state_changed`, `transition_started`, `transition_completed`, `beat_hit`, `bar_hit`
  - Last-call-wins for rapid `set_state()` calls

- **Test scene** with programmatic 3-state graph (exploration/combat/menu), UI buttons, layer toggles, beat flash indicator, event log

### Verification Items (Not Yet Tested at Runtime)

1. **Null streams** — Test graph uses `null` AudioStreams. `AudioStreamSynchronized` and `AudioStreamPlayer` may warn or error with null. Drop in real `.ogg` placeholder loops for proper testing.

2. **AudioStreamSynchronized volume** — Using `-80.0 dB` as silence, `0.0 dB` as full. Confirm Godot doesn't require special handling for "fully muted" state.

3. **BeatClock.process() ownership** — Called from `MusicSystem._process()`. Verify no other script also calls it (would double-emit signals). AudioManager does NOT call it currently.

4. **Wildcard transition `from → "*"`** — C++ `find_transition()` handles this as 3rd priority lookup. Test that `find_transition(&"menu", &"exploration")` correctly matches the `from=menu, to=*` rule.

5. **Interactive mode not exercised by test** — Test graph has layers → `_is_simple_graph()` returns false → always uses CUSTOM path. Need a separate simple graph (no layers, crossfade/cut only) to validate the AudioStreamInteractive codepath.

6. **Player swap after crossfade** — `_on_transition_complete` swaps `_player_a`/`_player_b`. Verify the swap logic doesn't conflict when transitioning from sync_player (layered state) to a non-layered state.

### Previous Phases Complete (for context)

- G2.1 — MusicStateGraph C++ Resource ✅ (`runtime/music_state_graph.h/.cpp`)
- G2.2 — BeatClock C++ ✅ (`runtime/beat_clock.h/.cpp`)
- G1 — Full foundation ✅ (SoundEvent, VoiceManager, EventDispatcher, AudioManager, bus layout, plugin)

### Next Up

Phases G1, G2, G3, and S1 are all complete. The v1.0 milestone is ~75% done. Next options:

- **G4 — Dialogue & Bus** — DialogueAudioPipeline, BusController snapshots, auto-ducking
- **G5 — Ambient & Spatial** — AudioZone2D, AmbientSystem, scatter layers
- **S2 — Synthesis Toolkit** — ModalBank, GrainCloud, PitchShifter, Waveshaper, FM, presets
- **G6 — Debug Tools** — performance overlay, event log, test suite

Recommended: G4 (completes the core runtime feature set) or S2 (enables rich procedural content).
