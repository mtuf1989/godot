# Phase 6 Implementation Plan — Optimize For Quest (Web + Scale)

**Created**: May 30, 2026  
**Last Updated**: May 30, 2026  
**Status**: ✅ COMPLETE (6/6 slices done)  
**Prerequisite**: Phase 5 complete ✓

---

## Slice Order & Status

| Slice | Scope | Status | Notes |
|-------|-------|--------|-------|
| 6D | Cross-Platform Timing | ✅ DONE | `symphony_platform_time.h` |
| 6F | AUDIO-type GraphInput | ✅ DONE | `GraphInputAudio` operator |
| 6E | WavePlayer Node | ✅ DONE | Reference + baked modes |
| 6B | Data-Driven Optimizations | ✅ DONE | Micro-block, SIMD flag, unroll hints |
| 6C | Voice Tiers 4+5 + Stealing Tests | ✅ DONE | Tests created, deadlock fixed |
| 6A | Web Export & WASM | ✅ DONE | Emscripten 5.0.7, builds clean, exported |

---

## Performance Results (M2 macOS, 512-sample buffer @ 44.1kHz)

Tested May 30, 2026. Voices spawned until budget stabilized ~52%.

| Tier | Voices | Budget | Peak | Avg µs/voice | Memory |
|------|--------|--------|------|--------------|--------|
| 1 (small, 5-8 nodes) | 620 | 51.0% | 56.5% | 10 µs | 50 MB |
| 2 (small+medium) | 230 | 52.3% | 60.3% | 26 µs | 48 MB |
| 3 (all complexities) | 163 | 51.6% | 72.0% | 37 µs | 47 MB |
| 4 (mixed, 80 target) | 135 | 52.8% | 62.1% | 45 µs | 47 MB |
| 5 (mixed, 120 target) | 140 | 52.6% | 58.9% | 44 µs | 47 MB |

**Verdict**: Massively exceeds Phase 5 targets (50 voices < 40% was the goal). Memory flat at ~47 MB — buffer liveness analysis NOT needed.

### Web Performance Projection

Native per-voice: ~45 µs (mixed). WASM slowdown: 2-3x → ~90-135 µs per voice.  
128-sample buffer deadline: 2.9ms.  
Projected web capacity: **15-20 mixed voices** or **25 small-graph voices**.  
`-msimd128` may improve this by 20-30%.

---

## Bugs Found & Fixed

| Bug | Severity | Fix |
|-----|----------|-----|
| `enforce_voice_limits()` deadlock — calls `stop()` while holding `registry_mutex`, `stop()` re-acquires same mutex | Critical | Collect victims in local Vector, release lock, then stop |
| WavePlayer reference mode — `get_data()` returns Vector copy, pointer dangles after `create()` returns | Critical | Store `Vector<uint8_t> pcm_data_copy` member to own the data |
| `enforce_voice_limits()` never called | Medium | Added call at end of `mix()` |

---

## Completed Slice Details

### Slice 6D: Cross-Platform Timing ✅

**Files created**: `modules/symphony/core/symphony_platform_time.h`  
**Files modified**: `modules/symphony/stream/audio_stream_playback_symphony.cpp`

Single inline function `symphony_time_usec()` with `#ifdef` for macOS/Windows/Emscripten/Linux.

### Slice 6F: AUDIO-type GraphInput ✅

**Files created**: `modules/symphony/nodes/io/symphony_graph_input_audio.h`  
**Files modified**: `register_types.cpp`, `symphony_graph_flattener.cpp`, `symphony_editor_plugin.cpp`

- Separate `GraphInputAudio` operator type (AUDIO output, no-op execute)
- Flattener recognizes it as sub-graph input interface, validates AUDIO-only connections
- Editor shows audio pin styling, SubGraph pin collection includes it

### Slice 6E: WavePlayer Node ✅

**Files created**: `modules/symphony/nodes/generators/symphony_wave_player.h`  
**Files modified**: `register_types.cpp`, `symphony_editor_plugin.h/.cpp`

- 16-bit PCM, mono/stereo (stereo downmixed to mono)
- Reference mode (stores Vector copy) + baked mode (PCM in arena)
- Linear interpolation, rate-scaling pitch
- Forward + ping-pong loop modes
- Gate trigger, finished trigger, auto_play
- State export/import for hot-swap
- Editor resource picker button for WAV files

### Slice 6B: Data-Driven Optimizations ✅

**Files modified**: `symphony_pin_types.h`, `SCsub`, + 5 operator headers

- `SYMPHONY_MICRO_BLOCK_SIZE`: 32 for Emscripten, 64 for native
- `-msimd128` flag in SCsub for web builds
- `SYMPHONY_UNROLL` macro applied to Oscillator, BiquadFilter, Gain, GraphOutput, MathAdd
- Buffer liveness analysis: **NOT NEEDED** (memory flat at 47 MB)

### Slice 6C: Voice Tiers 4+5 + Stealing Tests ✅

**Files created**: `tests/symphony/test_voice_stealing.gd`, `test_voice_stealing.tscn`  
**Files modified**: `tests/symphony/test_stress.gd`, `audio_stream_playback_symphony.cpp`, `symphony_voice_manager.cpp`

- 4 stealing test scenarios (max_voices, budget, RMS tiebreaker, no-glitch)
- Stress test extended with tier 4/5, peak budget, memory display
- `enforce_voice_limits()` called at end of `mix()`
- Deadlock fix (collect-then-stop pattern)

---

## Completed: Slice 6A — Web Export & WASM ✅

### Setup
- **Emscripten SDK**: 5.0.7 installed at `/Users/luong.pham/Work/emsdk`
- **Minimum required**: 4.0.0 (Godot enforces in `platform/web/detect.py`)

### Build Results
- **Command**: `scons platform=web target=template_debug arch=wasm32 module_symphony_enabled=yes`
- **Build time**: 5m10s
- **Output**: `bin/godot.web.template_debug.wasm32.zip` (9.4 MB)
- **WASM binary**: 35 MB
- **Compilation issues**: None. Only expected warnings:
  - `-pthread + ALLOW_MEMORY_GROWTH` performance note
  - `$GodotWebXR` depends on internal `$MainLoop` (WebXR internals)
- **SIMD**: `-msimd128` flag active via `modules/symphony/SCsub`

### Export Results
- **Export preset**: `tests/symphony/export_presets.cfg` (Web, template_debug)
- **Output directory**: `tests/symphony/web_export/`
- **Files**: index.html, index.wasm (35MB), index.pck (18KB), index.js (349KB), audio worklets, icons

### Testing
- **Server script**: `tests/symphony/serve_web.py` (port 8060 default)
- **Headers verified**: `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp`
- **To test**: `cd tests/symphony && python3 serve_web.py` → open http://localhost:8060 in Chrome

### Web Performance Notes
- Native per-voice: ~45 µs (mixed complexity)
- WASM expected slowdown: 2-3x → ~90-135 µs per voice
- 128-sample buffer deadline: 2.9ms
- **Projected web capacity**: 15-20 mixed voices, or 25 small-graph voices
- `-msimd128` may improve by 20-30%
- Browser testing required for actual measurements (run stress test in Chrome DevTools)

---

## Decisions Log

| Decision | Rationale |
|----------|-----------|
| Two operator types (GraphInput + GraphInputAudio) | Keeps compiler simple, no dynamic output types |
| WavePlayer stores Vector copy, not Ref<AudioStreamWAV> | `get_data()` returns copy; storing Vector owns the memory |
| Buffer liveness analysis skipped | Memory flat at 47 MB, audio buffers are ~3% of total |
| SIMD intrinsics skipped for native | Auto-vectorization working well (10 µs per small voice) |
| Micro-block 32 for web via #ifdef | Simpler than runtime config, no overhead |
| enforce_voice_limits() in mix() | Per-voice call is O(n²) but functional; proper hook deferred |
| Queue-next-wave deferred | Requires recompile; gate retrigger covers most use cases |
| Pitch-shifting deferred | Rate-scaling covers 99% of game audio; pitch-shift is a separate operator |

---

*Last updated: May 30, 2026*
