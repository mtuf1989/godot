# Dev Log — Symphony Audio System

## Known Limitations & Tech Debt

Items below are verified-actionable as of 2026-07-13. Each represents a real limitation, pending design decision, or known risk.

---

### Thread Safety & Concurrency

1. **~~LOD transition_to_lod() is not formally thread-safe~~** ✅ **RESOLVED** — `transition_to_lod()` now publishes via the `pending_graph` atomic slot with a `pending_is_lod` flag. The audio thread picks up the new graph at the next `mix()` block boundary and initiates the crossfade. All graph pointer mutations happen exclusively on the audio thread. *(stream/audio_stream_playback_symphony.cpp)*

2. **~~RTPCEngine `current_value` data race~~** ✅ **RESOLVED** — `current_value` is now `std::atomic<float>` with `memory_order_relaxed` for both audio-thread writes and game-thread reads. Zero performance cost on ARM64/x86-64 (lock-free). Eliminates formal UB and silences ThreadSanitizer. *(runtime/rtpc_engine.h, runtime/rtpc_engine.cpp)*

3. **~~BeatClock.process() has no double-call guard~~** ✅ **RESOLVED** — `process()` now checks `Engine::get_process_frames()` against a stored `last_process_frame` and early-returns if already called this frame. Prevents duplicate beat detection, double logic_time advancement, and over-applied drift correction. *(runtime/beat_clock.cpp)*

---

### Resource Management & Memory

4. **GrainCloud live-input shared pool (B1) not implemented**: Multiple live-input GrainCloud voices each allocate their own 384KB capture buffer. Unlike source_pcm mode (which uses SharedPCMCache), live-input buffers are unique per-voice and cannot be shared. Low priority since live-input granulation is rare in practice.

5. **GrainCloud arena waste in shared PCM mode**: When SharedPCMCache is used (B2), the arena still allocates a capture buffer (then `capture_buffer` pointer is overwritten to point at shared data). The arena allocation is wasted. Future optimization: skip arena alloc when `source_pcm_cache_key` is present. Saves 384KB arena space per shared voice.

---

### API Design & UX

6. **TriggerInput requires explicit trigger from game code**: One-shot graphs using TriggerInput won't produce sound unless game code calls `playback.trigger()` after `play()`. No auto-trigger-on-play mechanism exists. *(runtime/sound_event.h)*

7. **FilterCutoff RTPC target is a no-op**: `RTPC_FILTER_CUTOFF` (target=2) exists in enum but has no native implementation. Currently dead code. *(runtime/sound_event.h)*

8. **RTPC Volume: offset vs absolute semantics unresolved**: Design decision needed for how `volume_range` randomization interacts with RTPC volume binding. *(runtime/sound_event.h, game-template AudioManager)*

9. **BeatClock vs PhaseVocoder time-stretch convention mismatch**: BeatClock returns rate-based (>1.0 = faster), PhaseVocoder uses duration-based (>1.0 = slower). GDScript must invert with `1.0 / stretch`. *(runtime/beat_clock.cpp, nodes/spectral/symphony_phase_vocoder.h)*

---

### Integration & Cross-Layer Concerns

10. **BusController ducking offset drift**: `apply_snapshot()` does NOT reset `applied_duck_offsets`. Could cause volume drift when combining snapshots with auto-ducking. *(runtime/bus_controller.cpp)*

11. **Dialogue ducking stacks with category volume**: Effects stack (intentional) but could surprise users. Needs documentation. *(runtime/bus_controller.cpp)*

12. **VoicePool LOD thresholds are hardcoded (0.7/0.3)**: Ignores per-stream `lod_threshold_1/2` properties. Needs unification. *(runtime/voice_manager.cpp)*

13. **GrainCloud pin count increased (5→8)**: Old .tres files still work (unconnected=nullptr), but a migration concern for explicit pin-count validation. *(nodes/synthesis/symphony_grain_cloud.h)*

14. **GraphOutput does NOT sum multiple connections**: Must use Mix/MathAdd before GraphOutput. *(nodes/io/symphony_graph_output.h)*

15. **Operator StringName coupling with .tres files**: Renaming an operator silently breaks .tres graphs. No migration/alias system. *(core/symphony_operator_registry.h)*

---

### Game Audio Layer (game-template/) Concerns

16. **AudioZone2D editor performance**: `queue_redraw()` every frame. Needs throttling with 50+ zones. *(audio_zone_2d.gd)*

17. **Scatter timer leak on zone deactivation**: Entry persists in `_scatter_timers` dict. *(ambient_system.gd)*

18. **Listener position queried redundantly**: AmbientSystem and AudioManager both query Camera2D. *(ambient_system.gd + audio_manager.gd)*

19. **AudioZone2D shape caching is static**: Not tracked at runtime. *(audio_zone_2d.gd)*

20. **AudioStreamPlayer2D.max_distance=99999 workaround**: Fragile magic number. *(audio_manager.gd)*

21. **Scatter collision_mask silent failure**: No debug warning on miss. *(ambient_system.gd)*

22. **No loop_event hot-swap at runtime**: Use `set_variant()` instead. *(audio_zone_2d.gd)*

---

### Testing & CI

23. **Embree crash (macOS unit tests)**: Editor crashes fixed, but unit test runner may still hit pre-existing Godot embree shutdown issue. Workaround: `module_raycast_enabled=no`. *(tests/)*

24. **Staggered importance in tests**: Must call `process_frame()` 4+ times. *(runtime/voice_manager.cpp)*

25. **EventDispatcher cooldown uses instance_id**: Non-obvious resource sharing semantics. *(runtime/event_dispatcher.cpp)*

26. **Event names blank for dynamic Resources**: `get_path()` and `get_name()` both empty. *(runtime/event_dispatcher.cpp)*

27. **Null stream entries not validated**: Passes dispatcher but fails downstream. *(runtime/event_dispatcher.cpp)*

28. **AudioStreamInteractive codepath untested**: Test coverage gap. *(music_system.gd)*

29. **MusicSystem player swap after crossfade**: Integration pitfall. *(music_system.gd)*

---

### Compiler Warnings

30. **Member initializer order must match declaration order**: `-Wreorder-ctor` is an error. *(all new operators)*

---

## User Guide Topics (to document)

The following topics need proper user-facing documentation before the module is used by others. Currently they exist only as dev-log notes or source comments.

### GrainCloud Best Practices
- Default capture buffer is 2s. Set `capture_seconds` explicitly if you need more (up to 10s desktop, 4s web).
- For source_pcm presets: always pass `source_pcm_cache_key` (resource path) to enable memory sharing across voices.
- LOD tier 2 graphs should NOT use GrainCloud. Replace with Noise→Filter→Gain for distant voices.
- Live-input granulation allocates 384KB per voice — budget accordingly.

### Virtualization & Memory
- Connect to `SymphonyVoicePool.voice_virtualized` / `voice_devirtualized` signals.
- On virtualize: call `player.stop()` to release graph memory.
- On devirtualize: call `player.play()` to recompile. First ~50ms may be silent for live-input mode (masked by fade-in).

### PhaseVocoder Time-Stretch
- `time_stretch = 2.0` = output is 2× longer (half-speed). Matches DAW convention.
- If using BeatClock's `calculate_time_stretch_for_alignment()` output, invert with `1.0 / stretch`.

### TriggerInput One-Shots
- Graphs with TriggerInput nodes require explicit `playback.trigger(&"name")` call after `play()`.
- Without it, the graph runs but the ADSR/envelope never fires.

### BusController Snapshots & Ducking
- Ducking effects stack with `set_category_volume()`. Both modify same bus.
- After `apply_snapshot()`, ducking offsets may drift. Be aware when mixing snapshots + auto-ducking.

### LOD System
- 3 tiers: LOD 0 (full), LOD 1 (simplified), LOD 2 (minimal).
- Thresholds: 30% / 70% of max_distance with 5% hysteresis.
- LOD crossfade: 2048 samples (~42ms) parallel execution of both graphs.
- `force_lod()` / `release_lod_force()` for manual control.

### GraphOutput & Multi-Source Mixing
- GraphOutput has a single input pin. Use Mix/MathAdd nodes to combine multiple sources before output.

### Operator Naming & .tres Compatibility
- .tres files reference operators by StringName. Never rename operators without an alias/migration plan.

---

## Implemented Improvements: GrainCloud Memory

### Phase A — Default Reduction ✅
- A1: Default `capture_seconds` 4s → 2s (384KB instead of 768KB)
- A2: Web platform clamp to 4s max

### Phase B — SharedPCMCache ✅
- `core/shared_pcm_cache.h/.cpp` — Ref-counted singleton
- GrainCloud `create()` uses cache when `source_pcm_cache_key` param is provided
- N voices on same source = 1× memory

**Usage:**
```
node.params["source_pcm_ptr"] = (int64_t)pcm_pointer;
node.params["source_pcm_length"] = (float)sample_count;
node.params["source_pcm_cache_key"] = "res://audio/samples/wind_loop.wav";
```

### Phase C — Virtual Voice Signals ✅
- `SymphonyVoicePool` signals: `voice_virtualized(slot_index)`, `voice_devirtualized(slot_index)`
- GDScript connects → stops player on virtualize (frees graph) → plays on devirtualize (recompiles)

### Phase D — LOD Bypass (authoring guidance)
- LOD 2 presets should avoid GrainCloud entirely. Use lightweight alternatives for distant voices.

### Memory Budget (achieved)

| Scenario | Before | After |
|----------|--------|-------|
| 1 voice (default) | 768 KB | 384 KB |
| 8 voices (same source) | 6.1 MB | 384 KB shared |
| 8 voices (virtualized) | 6.1 MB | ~0 KB |
| Web (4 voices, worst case) | 3.1 MB | 384 KB–1.5 MB |

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
