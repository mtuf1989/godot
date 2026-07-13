# Dev Log — Symphony Audio System

## Known Limitations & Tech Debt

Items below are verified-actionable as of 2026-07-13. Each represents a real limitation, pending design decision, or known risk that future sessions should be aware of.

---

### Thread Safety & Concurrency

1. **LOD transition_to_lod() is not formally thread-safe**: Main thread directly swaps `current_graph` while `mix()` reads it on the audio thread. Currently safe because Godot's AudioStreamPlayback is accessed by one thread at a time, but if AudioServer ever parallelizes playback mixing, this needs the atomic `pending_graph` swap pattern. *(stream/audio_stream_playback_symphony.cpp)*

2. **RTPCEngine `current_value` data race**: Game thread reads `current_value` via `get_current_value()` while audio thread writes it during smoothing. `target_value` uses `std::atomic<float>`, but `current_value` is a plain float. Practically safe on ARM64/x86 (naturally-aligned float stores are atomic at hardware level) but formally undefined behavior in C++. Consider `std::atomic<float>` for `current_value` if issues arise. *(runtime/rtpc_engine.cpp)*

3. **BeatClock.process() has no double-call guard**: If two GDScript systems both call `BeatClock.process()` in the same frame, beats get detected/emitted twice and drift correction doubles. No ownership assertion exists. Integrators must ensure exactly one caller per frame. *(runtime/beat_clock.cpp)*

---

### Resource Management & Memory

4. **GrainCloud arena allocation is large**: Default 4s capture at 48kHz = ~768KB per instance. Max 10s = ~1.88MB. With 8 simultaneous GrainCloud voices = 6-15MB of arena. Important for voice budget planning and web targets. See improvement plan below. *(nodes/synthesis/symphony_grain_cloud.h)*

5. **GrainCloud source_pcm_ptr uses reinterpret_cast from int64**: PCM pointer is passed as int64 in the params HashMap (Variant can't hold raw pointers). Only safe because compilation happens on main thread and AudioStreamWAV resource lifetime exceeds the arena copy. Must never be exposed to user-facing APIs. **Decision: keep as-is** — the pattern is confined to the graph compiler → `create()` path, data is memcpy'd immediately, and no user-facing code touches it. *(nodes/synthesis/symphony_grain_cloud.h)*

---

### API Design & UX

6. **TriggerInput requires explicit trigger from game code**: One-shot graphs using TriggerInput (e.g., footstep's "pluck") won't produce sound unless game code calls `playback.trigger()` after `play()`. No auto-trigger-on-play mechanism exists. Consider adding auto-trigger support to SoundEvent for one-shot use cases. *(runtime/sound_event.h)*

7. **FilterCutoff RTPC target is a no-op**: `RTPC_FILTER_CUTOFF` (target=2) exists in the SoundEvent::RTPCTarget enum but has no native C++ implementation. Would require per-voice filter via AudioEffect or GraphInput binding. Currently dead code. *(runtime/sound_event.h)*

8. **RTPC Volume: offset vs absolute semantics unresolved**: When SoundEvent has both `volume_range` (randomization) and RTPC binding targeting RTPC_VOLUME, the interaction depends on how the GDScript layer applies them. Is RTPC additive (offset from randomized initial) or absolute (overrides it)? Design decision needed. *(runtime/sound_event.h, game-template AudioManager)*

9. **BeatClock vs PhaseVocoder time-stretch convention mismatch**: `BeatClock.calculate_time_stretch_for_alignment()` returns rate-based values (>1.0 = faster). PhaseVocoder uses duration-based convention (>1.0 = longer/slower). GDScript layer feeding BeatClock output into PhaseVocoder must invert with `1.0 / stretch`. *(runtime/beat_clock.cpp, nodes/spectral/symphony_phase_vocoder.h)*

---

### Integration & Cross-Layer Concerns

10. **BusController ducking offset drift**: `applied_duck_offsets` tracks cumulative ducking deltas. If bus volumes are reset externally (e.g., `apply_snapshot()` sets absolute values), offsets become stale. `apply_snapshot()` does NOT reset `applied_duck_offsets`. Could cause volume drift when combining snapshots with auto-ducking. *(runtime/bus_controller.cpp)*

11. **Dialogue ducking stacks with category volume**: BusController ducking + GDScript `AudioManager.set_category_volume()` both modify the same bus volume. Effects stack (intentional, same as Wwise) but could surprise users. Needs documentation. *(runtime/bus_controller.cpp, game-template AudioManager)*

12. **VoicePool LOD thresholds are hardcoded, ignoring per-stream config**: `voice_manager.cpp` hardcodes `0.7f`/`0.3f` thresholds. Meanwhile `AudioStreamSymphony` exposes configurable `lod_threshold_1`/`lod_threshold_2` properties used only in `get_recommended_lod()`. The VoicePool doesn't read per-stream thresholds — needs unification. *(runtime/voice_manager.cpp, stream/audio_stream_symphony.h)*

13. **GrainCloud pin count increased (5→8)**: Old graph .tres files with only 5 connections still work (unconnected pins default to nullptr with fallback values). But the graph compiler must ensure it allocates the correct number of pin slots for old resources. Migration concern for existing .tres files. *(nodes/synthesis/symphony_grain_cloud.h)*

14. **GraphOutput does NOT sum multiple connections**: It has a single `input` pin. Preset graphs that need multiple audio sources combined into output must use Mix/MathAdd nodes before GraphOutput. *(nodes/io/symphony_graph_output.h)*

15. **Operator StringName coupling with .tres files**: .tres graph descriptions reference operators by StringName (e.g., `&"ModalBank"`). Renaming an operator in C++ silently breaks all referencing .tres files (graceful degradation: logs error, produces silence). No migration/alias system exists. *(core/symphony_operator_registry.h)*

---

### Game Audio Layer (game-template/) Concerns

16. **AudioZone2D editor performance**: `queue_redraw()` every frame in editor for debug boundary drawing. With 50+ zones visible, this causes editor stutter. Needs throttling or dirty-flag approach. *(game-template, audio_zone_2d.gd)*

17. **Scatter timer leak on zone deactivation**: When a zone is evicted from AmbientSystem's active set, its entry in `_scatter_timers` dict persists. Minor memory concern for levels with many transient zones. *(game-template, ambient_system.gd)*

18. **Listener position queried redundantly**: AmbientSystem and AudioManager independently query Camera2D each frame. Should consolidate to single source of truth to avoid 1-frame desync and wasted cycles. *(game-template, ambient_system.gd + audio_manager.gd)*

19. **AudioZone2D shape caching is static**: `_cached_shapes` populated in `_ready()` only. Runtime CollisionShape2D additions/removals are not tracked. Dynamic zones require manual `_cache_collision_shapes()` call. *(game-template, audio_zone_2d.gd)*

20. **AudioStreamPlayer2D.max_distance workaround**: Set to 99999.0 to disable Godot's built-in 2D attenuation (Symphony manages its own). Fragile magic number — switch to Godot's "disable attenuation" flag if one is ever added. *(game-template, audio_manager.gd)*

21. **Scatter collision_mask silent failure**: Physics query uses zone's `collision_mask`. Misconfigured mask = all 5 scatter retries fail silently (no error, event just skipped). Needs debug warning in development builds. *(game-template, ambient_system.gd)*

22. **No loop_event hot-swap at runtime**: Changing `AudioZone2D.loop_event` while zone is active doesn't trigger crossfade. Use `AmbientSystem.set_variant()` for runtime switching. Direct property changes require stop-and-restart (audible gap). *(game-template, audio_zone_2d.gd)*

---

### Testing & CI

23. **Unit test runner may still crash on macOS with Embree**: The editor/preview crashes (manifesting as embree `TaskScheduler::removeScheduler` crash) were caused by Symphony's own heap corruption — fixed in `d5cbbb06bc` (preview thread compilation skipped, arena sizing fixed). However, the unit test runner (`tests/modules/test_symphony_operators.cpp`) may still trigger the pre-existing Godot embree shutdown issue. Workaround: `module_raycast_enabled=no` for test builds, or test on Linux CI. Needs re-verification. *(tests/modules/test_symphony_operators.cpp)*

24. **Staggered importance requires multiple process_frame() calls in tests**: `update_importance()` uses `IMPORTANCE_UPDATE_INTERVAL = 4` (1/4 of pool per frame). Tests checking importance must call `process_frame()` at least 4 times after changing positions. *(runtime/voice_manager.cpp)*

25. **EventDispatcher cooldown uses instance_id**: Same SoundEvent resource via different load paths = different instances = independent cooldowns. Same resource shared = shared cooldowns globally. Non-obvious resource management consequence. *(runtime/event_dispatcher.cpp)*

26. **Event names blank for dynamically-created Resources**: `get_path().get_file()` returns empty for unsaved Resources. Fallback `get_name()` also empty for programmatic `SoundEvent.new()`. Logs show blank event names. *(runtime/event_dispatcher.cpp)*

27. **Null stream entries not validated**: EventDispatcher rejects events with zero streams, but individual null entries within a non-empty `streams` array are not checked. Would pass dispatcher but fail downstream on play. *(runtime/event_dispatcher.cpp)*

28. **AudioStreamInteractive codepath untested**: No test exercises the interactive music transition path (simple graphs without layers). Test coverage gap. *(game-template, music_system.gd)*

29. **MusicSystem player swap after crossfade**: The AudioStreamPlayer reference swap when crossfade completes is an integration pitfall — verify swap logic doesn't conflict when transitioning between layered (sync_player) and non-layered states. *(game-template, music_system.gd)*

---

### Compiler Warnings

30. **Member initializer order must match declaration order**: `-Wreorder-ctor` is treated as an error. FDN Reverb previously hit this. Always verify class member declaration order before writing constructor initializer lists. *(applies to all new operators)*

---

## Improvement Plan: GrainCloud Memory (Item 4)

**Goal**: Reduce per-voice memory from ~768KB (default) / ~1.88MB (max) to a sustainable budget, especially for web targets and scenes with multiple granular voices.

### Phase A — Quick Wins (no API changes)

**A1. Reduce default `capture_seconds` from 4s to 2s**
- Most use cases (wind, texture, ambience) don't need 4 seconds of capture history.
- 2s × 48kHz = 96,000 floats = **384 KB** (50% reduction).
- Change default in `register_operator()` and all presets that don't explicitly need 4s.
- Effort: trivial. Risk: none (existing presets that set `capture_seconds` explicitly are unaffected).

**A2. Clamp `max_capture_seconds` to 4s on web export**
- Add a platform check in `create()`: if `OS::get_singleton()->has_feature("web")`, clamp to 4s regardless of param.
- Prevents worst-case 1.88MB on memory-constrained platforms.
- Effort: trivial.

### Phase B — Shared Capture Pool

**B1. Introduce `CaptureBufferPool` singleton**
- A simple arena that pre-allocates N shared capture buffers (e.g., 4 × 2s = 1.5MB total).
- GrainCloud voices in "live input" mode request a slot from the pool instead of allocating their own.
- When the pool is full, new voices get a smaller buffer (1s) or are rejected.
- Voices in "source PCM" mode (static data from .tres) still use per-voice arena copy (data is unique per-resource).

**B2. Read-only shared source buffers**
- When multiple GrainCloud voices play the same `source_pcm_ptr` data (same AudioStreamWAV resource), detect this at compile time and share one arena copy.
- The graph compiler tracks `{resource_id → arena_ptr}` in a compile-session cache.
- All voices get a read-only pointer to the shared copy.
- Savings: N voices on same source = 1× memory instead of N×.
- Effort: medium. Requires graph compiler changes + lifetime tracking.

### Phase C — Virtual Voice Memory Release

**C1. On virtualization, release arena**
- When VoiceManager transitions a GrainCloud voice to VIRTUAL state, destroy the `CompiledGraph` (and its arena).
- Keep a lightweight `VirtualVoiceStub` with: resource path, last position, last params, elapsed time.
- On devirtualization, recompile the graph from the stub (cold start, ~1 frame latency).
- Saves: 100% memory for inaudible voices.

**C2. Warm devirtualization (optional)**
- Pre-fill the capture buffer with source PCM on devirtualize (if source_pcm mode).
- For live-input mode, accept that the first 50-100ms after devirtualize is "dry" (capture buffer filling up).
- Mask with a short fade-in (already handled by voice fade system).

### Phase D — LOD Bypass (already supported)

- LOD tier 2 graphs should NOT use GrainCloud at all. Replace with Noise→Filter→Gain for distant voices.
- The LOD system (`AudioStreamSymphony.lod_graphs`) already supports this — just author appropriate LOD 2 presets.
- Document as best practice for preset authors: "GrainCloud is LOD 0/1 only."

### Priority Order

1. **A1** (immediate, trivial) → ship with next commit
2. **A2** (immediate, trivial) → same commit
3. **D** (documentation, no code) → update preset authoring guide
4. **B2** (medium effort, high impact for source_pcm presets)
5. **C1** (medium effort, high impact for voice count scaling)
6. **B1** (lower priority — live-input GrainCloud voices are rarer in practice)

### Memory Budget Targets

| Scenario | Current | After A1+A2 | After B2 | After C1 |
|----------|---------|-------------|----------|----------|
| 1 GrainCloud voice (default) | 768 KB | 384 KB | 384 KB | 384 KB |
| 8 GrainCloud voices (same source) | 6.1 MB | 3.1 MB | 384 KB | 0 KB (if virtualized) |
| 8 GrainCloud voices (different sources) | 6.1 MB | 3.1 MB | 3.1 MB | 0 KB (if virtualized) |
| Web target (worst case, 4 voices) | 3.1 MB | 1.5 MB | 384 KB–1.5 MB | 0 KB |

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
