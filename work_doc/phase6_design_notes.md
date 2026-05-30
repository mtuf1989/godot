# Phase 6 Design Notes — Optimize For Quest (Web + Scale)

## Status: NOT STARTED (deferred from Phase 5)

## Scope

Phase 6 picks up everything deferred from Phase 5:

### 6A: Web Export & WASM Testing
- Build Godot with Emscripten (web export template)
- Verify Symphony module compiles to WASM
- Test with 128-sample buffer (2.9ms deadline)
- Target: 25 mixed-complexity voices on Web < 60% budget
- Identify WASM-specific bottlenecks (no NEON, no SIMD.js unless explicitly used)
- Consider WebAssembly SIMD (128-bit, available in modern browsers)

### 6B: Advanced Optimizations (Data-Driven)
Based on Phase 5 profiling results, implement as needed:

| Optimization | Trigger Condition | Expected Gain |
|-------------|-------------------|---------------|
| SIMD intrinsics (NEON/SSE) | Auto-vectorization report shows missed opportunities | 2-4x on hot operators |
| Buffer liveness analysis | Memory > 100MB at 80+ voices | 30-60% memory reduction |
| Worker thread compilation | Compile time > 50ms for large graphs | Editor stays responsive |
| Lock-free command queue | Parameter updates show measurable latency | Eliminates rare stalls |
| Loop unrolling pragmas | Compiler not unrolling 64-iteration loops | 5-10% |

### 6C: Voice Tiers 4+5
- Tier 4: 80 voices mixed complexity
- Tier 5: 120 voices mixed complexity
- Stress test on M2 macOS + Web
- Identify scaling bottlenecks (cache pressure, memory bandwidth)

### 6D: Cross-Platform Validation
- Windows (x86_64): verify SSE2 auto-vectorization
- Linux: verify with PulseAudio/PipeWire backends
- Web: AudioWorklet with 128-sample buffers
- (Optional) Android: verify with OpenSL ES / AAudio

### 6E: WavePlayer Node
- Implement WavePlayer operator (deferred from Phase 4)
- Needs resource picker widget in editor (not just SpinBox)
- Plays AudioStreamWAV with pitch control, loop points
- Sample-accurate concatenation (queue next wave)

### 6F: AUDIO-type GraphInput
- Allow sub-graphs to accept audio-rate input from parent
- GraphInput with pin_type=AUDIO outputs 64-sample buffer
- Flattener aliases parent's source buffer directly (no operator, no copy)
- Enables audio-processing sub-graphs (e.g., "reverb patch" that takes audio in)

---

## Key Constraints for Web

- WASM runs ~1.5-3x slower than native ARM64
- No NEON auto-vectorization in WASM (must use WebAssembly SIMD explicitly)
- 128-sample buffer = 2.9ms deadline (very tight)
- SharedArrayBuffer required for AudioWorklet (COOP/COEP headers needed)
- Godot's web audio uses AudioWorklet since 4.0

## Target Budget (Web)

- 25 voices mixed complexity: < 60% of 2.9ms = < 1.74ms total Symphony processing
- Per voice average: < 70µs per mix() call (128 frames = 2 micro-blocks)
- If not achievable: reduce micro-block count, or accept lower voice count on web

---

## Dependencies on Phase 5

- Phase 5 profiling data determines which optimizations from 6B are needed
- Phase 5 voice manager is prerequisite for tier 4+5 testing
- Phase 5 stress test scene is reused for web testing

---

*Created: May 30, 2026*
*Prerequisite: Phase 5 complete*
