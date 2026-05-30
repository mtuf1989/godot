# Phase 5 Design Notes — Profiling & Voice Management

## Status: IN PROGRESS (as of 2026-05-30)

## Scope

Phase 5 delivers:
1. Built-in profiling instrumentation (per-voice timing, budget tracking)
2. Voice management system (priority + quietest stealing)
3. GDScript stress-test scene
4. Measurement runs for tiers 1-3 (10, 25, 50 voices)
5. Trivial optimizations (`__restrict__`, loop hints)

## Deferred to Phase 6
- Web export + WASM testing
- SIMD intrinsics (if auto-vectorization insufficient)
- Buffer liveness analysis (if memory pressure observed)
- Worker thread compilation (if compile time > 50ms)
- Lock-free command queue (if contention observed)
- Voice tiers 4+5 (80, 120 voices)
- Cross-platform validation (Windows, Linux, Web)

---

## Target Hardware

- Apple M2, 24GB RAM, macOS
- Audio: CoreAudio, 512 samples @ 44.1kHz (11.6ms deadline)
- Budget target: Symphony total < 40% of deadline (~4.6ms)

---

## Measurements

| Metric | Implementation |
|--------|---------------|
| Per-voice micro-block time (µs) | `mach_absolute_time()` around `current_graph->execute()` |
| Total mix() time (µs) | Timer around entire `mix()` body |
| Budget utilization % | `total_mix_time / (buffer_size / sample_rate * 1e6) * 100` |
| Peak budget % | Track max over rolling window |
| Arena memory per voice (bytes) | `current_graph->arena.size()` |
| Compilation time (ms) | Timer around `compile_graph()` |
| Active voice count | Count of playing AudioStreamPlaybackSymphony instances |

Exposed to GDScript via a singleton or static methods on AudioStreamPlaybackSymphony.

---

## Voice Management Design

### Properties

On `AudioStreamSymphony` resource (designer-facing):
- `voice_priority` (int, 0-100, default 50) — higher = harder to steal
- `max_voices` (int, 0 = unlimited, default 0) — global voice limit

On `AudioStreamPlaybackSymphony` (runtime):
- `set_priority(int)` — override resource default
- `get_voice_cpu_microseconds()` — last measured cost
- `get_budget_percent()` — current budget utilization

### Stealing Policy

1. When active voices > global limit:
   - Sort by priority ASC, then by RMS output ASC (quietest first)
   - Stop lowest-priority, quietest voice
2. When total budget > 70% (warning threshold):
   - Log warning
3. When total budget > 90% (critical threshold):
   - Force-stop lowest-priority voice until under 70%

### RMS Tracking

- Computed cheaply in `mix()`: sum of squared samples from GraphOutput, divided by frame count, sqrt
- Updated once per `mix()` call (not per micro-block)
- Stored as `float last_rms` on the playback instance

---

## Voice Tiers (Test Graphs)

| Tier | Voices | Graph Complexity | Nodes/Voice | Total Nodes |
|------|--------|-----------------|-------------|-------------|
| 1 | 10 | Small | 5-8 | 50-80 |
| 2 | 25 | Mixed (small + medium) | 8-15 | 200-375 |
| 3 | 50 | Mixed (small + medium + large) | 5-25 | 250-1250 |
| 4 | 80 | Mixed | 5-25 | 400-2000 |
| 5 | 120 | Mixed | 5-25 | 600-3000 |

Tiers 4-5 deferred to Phase 6.

### Test Graph Templates

**Small (gunfire/footstep):** Noise → BiquadFilter → ADSR → Gain → GraphOutput (5 nodes)

**Medium (engine/ambient):** Oscillator×2 → Mix → BiquadFilter → Gain → Compressor → GraphOutput + LFO → filter cutoff (8-10 nodes)

**Large (music/complex):** Multiple oscillators, filters, envelopes, sub-graphs, 25+ nodes

---

## Implementation Plan

### Slice A: Profiling Infrastructure
- Add timing to `mix()` and per-voice `execute()`
- Add `last_rms` tracking
- Expose metrics via GDScript API
- Add `SymphonyVoiceManager` singleton for global tracking

### Slice B: Voice Manager
- `voice_priority` property on AudioStreamSymphony
- Global voice registry (tracks all active playbacks)
- Stealing logic (priority + quietest)
- Budget threshold warnings + force-stop

### Slice C: Stress Test Scene
- GDScript scene that spawns N AudioStreamPlayers
- Configurable: voice count, graph complexity tier, ramp-up speed
- HUD showing: active voices, budget %, per-voice avg µs, memory
- Programmatic graph building (no need for .tres files)

### Slice D: Trivial Optimizations
- `__restrict__` on pin pointers in operator execute() methods
- Fixed loop count hints for 64-sample micro-block
- Measure before/after

---

## Success Criteria

| Criterion | Target |
|-----------|--------|
| 50 mixed-complexity voices on macOS (512 buffer) | < 40% budget utilization |
| Voice manager prevents dropout | No audible glitches when exceeding limit |
| Per-voice timing exposed to GDScript | `get_voice_cpu_microseconds()` works |
| Compilation time for 50-node graph | < 5ms |
| Memory per voice (25-node graph) | < 64KB |
| Stress test scene runs without crash | Stable for 60+ seconds |

---

*Created: May 30, 2026*
