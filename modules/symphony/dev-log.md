# Dev Log — Symphony Audio System

## Known Limitations & Tech Debt

Items below are verified-actionable as of 2026-07-13. Each represents a real limitation, pending design decision, or known risk.

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
