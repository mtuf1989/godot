# Phase 6 Implementation Plan — Optimize For Quest (Web + Scale)

**Created**: May 30, 2026  
**Status**: NOT STARTED  
**Prerequisite**: Phase 5 complete ✓

---

## Slice Order

| Slice | Scope | Est. Complexity |
|-------|-------|-----------------|
| 6D | Cross-Platform Timing | Small |
| 6F | AUDIO-type GraphInput | Small |
| 6E | WavePlayer Node | Medium |
| 6B | Data-Driven Optimizations | Medium |
| 6C | Voice Tiers 4+5 + Stealing Tests | Medium |
| 6A | Web Export & WASM | Large |

---

## Slice 6D: Cross-Platform Timing

**Goal**: Replace macOS-only `mach_absolute_time()` with per-platform high-resolution timers.

### Files to Modify
- `modules/symphony/stream/audio_stream_playback_symphony.cpp`

### Implementation

Create a small inline helper (in a new header `core/symphony_platform_time.h`):

```
Platform        | API                          | Resolution
----------------|------------------------------|----------
macOS           | mach_absolute_time()         | ~ns
Linux           | clock_gettime(CLOCK_MONOTONIC) | ~ns
Windows         | QueryPerformanceCounter()    | ~100ns
Web (Emscripten)| emscripten_get_now() * 1000  | ~µs (float ms)
```

Interface:
```cpp
static inline uint64_t symphony_time_usec(); // Returns microseconds
```

Use `#if defined(__APPLE__)`, `#elif defined(_WIN32)`, `#elif defined(__EMSCRIPTEN__)`, `#else` (Linux/POSIX).

### Acceptance Criteria
- [ ] Builds on macOS (verified)
- [ ] No functional change to profiling output on macOS
- [ ] Compiles cleanly with `-Werror` on all `#ifdef` branches (syntax check)

---

## Slice 6F: AUDIO-type GraphInput

**Goal**: Allow sub-graphs to accept audio-rate input from parent graph.

### Design

`SymphonyGraphInput` currently outputs a single `float` (control-rate). When `pin_type=AUDIO` (value 0 in the enum), it should instead:
- Output a 64-sample audio buffer
- Accept audio-rate writes from the parent (via flattener aliasing)
- When standalone (no parent connection), output silence

### Files to Modify
- `modules/symphony/nodes/io/symphony_graph_input.h` — Add AUDIO mode
- `modules/symphony/core/symphony_graph_compiler.cpp` — Allocate audio buffer when pin_type=AUDIO
- `modules/symphony/core/symphony_graph_flattener.cpp` — Validate AUDIO-type connections (already aliases correctly)
- `modules/symphony/editor/symphony_editor_plugin.cpp` — Show audio pin styling for AUDIO-type GraphInput

### Implementation Details

1. **GraphInput AUDIO mode**: When `pin_type == 0` (AUDIO):
   - `output` becomes `float*` pointing to a 64-sample buffer (allocated by compiler)
   - `execute()` is a no-op (buffer is written by parent via aliasing, or stays zeroed)
   - `set_value()` is ignored in AUDIO mode (no scalar to set)
   - Register with output pin type `SymphonyPinType::AUDIO` instead of `FLOAT`

2. **Compiler change**: In Phase 7 (create operators), check `pin_type` param. If AUDIO, register the output as an audio buffer. The `register_operator()` descriptor needs a dynamic output type — or we register two variants: `GraphInput` (FLOAT) and `GraphInputAudio` (AUDIO).

   **Decision**: Use two separate operator types internally (`GraphInput` and `GraphInputAudio`) to keep the compiler simple. The editor shows them as one node type with a `pin_type` dropdown.

3. **Flattener**: No change needed — it already rewires parent output → sub-graph internal consumers. Just validate that the parent's output type matches (AUDIO→AUDIO, reject FLOAT→AUDIO-type-GraphInput without promotion).

4. **Editor**: When a GraphInput node has `pin_type=AUDIO`, show the output slot with audio color/icon.

### Acceptance Criteria
- [ ] Sub-graph with AUDIO-type GraphInput compiles
- [ ] Parent graph feeding audio into sub-graph works (sound passes through)
- [ ] Standalone sub-graph with AUDIO-type GraphInput outputs silence (no crash)
- [ ] Editor shows correct pin styling
- [ ] Type mismatch (Float→AUDIO GraphInput) produces compile error

---

## Slice 6E: WavePlayer Node

**Goal**: Implement `SymphonyWavePlayer` operator that plays AudioStreamWAV samples with pitch control and loop points.

### Design

Two modes:
- **Reference mode** (default): Holds resource path, loads PCM data pointer at compile time from the AudioStreamWAV resource. Zero-copy read from the resource's internal buffer.
- **Baked mode**: Copies PCM data into the arena at compile time. Faster (guaranteed cache locality), uses more memory.

### Operator Interface

**Inputs**:
| Pin | Type | Required | Description |
|-----|------|----------|-------------|
| gate | TRIGGER | No | Trigger to start/restart playback |
| pitch | FLOAT | No | Pitch multiplier (default 1.0) |

**Outputs**:
| Pin | Type | Description |
|-----|------|-------------|
| output | AUDIO | 64-sample mono output |
| finished | TRIGGER | Fires when playback reaches end (non-looping) |

**Parameters**:
| Param | Type | Description |
|-------|------|-------------|
| resource_path | String | Path to AudioStreamWAV resource |
| bake_audio | bool | If true, copy PCM into arena |
| loop_mode | int | 0=none, 1=forward, 2=ping-pong |
| loop_start | int | Loop start in samples |
| loop_end | int | Loop end in samples (0 = end of file) |
| pitch_scale | float | Base pitch multiplier |
| auto_play | bool | Start playing immediately on graph start |

### Implementation Details

1. **PCM access**: At compile time (`create_fn`), load the AudioStreamWAV via ResourceLoader, get its raw PCM data pointer and length. If `bake_audio`, memcpy into arena. Otherwise, store the raw pointer (resource stays loaded as long as the AudioStreamSymphony resource is alive).

2. **Playback**: Maintain a `double read_position` (fractional sample index). Each `execute()`:
   - Advance read_position by `pitch_scale * pitch_input` per output sample
   - Interpolate (linear) between adjacent PCM samples
   - Handle loop points and end-of-file

3. **Queue-next-wave**: Add `queue(resource_path)` method accessible via trigger. When current wave finishes, seamlessly start the queued wave at sample 0. Implementation: store a `next_resource_path` that gets picked up on `finished` trigger.
   - **Note**: Queue requires a recompile (new PCM data). Alternative: pre-load N wave slots at compile time. **Decision**: Defer queue to a follow-up. For now, support single-wave playback with gate retrigger.

4. **Editor widget**: Resource picker (file dialog filtered to `.wav`/`.tres` AudioStreamWAV) instead of a SpinBox for the `resource_path` parameter. Add to the inspector proxy's `_get_property_list`.

### Files to Create
- `modules/symphony/nodes/generators/symphony_wave_player.h`

### Files to Modify
- `modules/symphony/core/symphony_operator_registry.cpp` — Register WavePlayer
- `modules/symphony/register_types.cpp` — Include and register
- `modules/symphony/editor/symphony_editor_plugin.cpp` — Resource picker for WAV params

### Acceptance Criteria
- [ ] WavePlayer plays a loaded AudioStreamWAV correctly
- [ ] Pitch scaling works (2.0 = octave up, 0.5 = octave down)
- [ ] Loop modes work (forward, ping-pong)
- [ ] Gate trigger restarts playback
- [ ] Finished trigger fires at end of non-looping playback
- [ ] Baked mode works (PCM in arena)
- [ ] Reference mode works (reads from resource)
- [ ] Editor shows resource picker for WAV selection
- [ ] No crash on missing/invalid resource path

---

## Slice 6B: Data-Driven Optimizations

**Goal**: Implement optimizations based on Phase 5 profiling results and web requirements.

### 6B.1: Configurable Micro-Block Size

**Current**: `SYMPHONY_MICRO_BLOCK_SIZE = 64` (hardcoded in `symphony_pin_types.h`)

**Change**: Make it a project setting with compile-time default:
```
audio/symphony/micro_block_size = 64  (native default)
audio/symphony/micro_block_size = 32  (web recommendation)
```

**Implementation**: 
- Keep the `constexpr` for compile-time buffer allocation (operators allocate fixed buffers)
- For web builds, `#ifdef __EMSCRIPTEN__` sets default to 32
- The value must be a power of 2, ≤ 64
- All operators already use `p_num_frames` parameter, so they work with any size ≤ 64

**Alternative (simpler)**: Two compile-time constants — `SYMPHONY_MICRO_BLOCK_SIZE_NATIVE = 64`, `SYMPHONY_MICRO_BLOCK_SIZE_WEB = 32`. Selected by `#ifdef`. No runtime configurability needed since buffer sizes are baked into the arena.

**Decision**: Use the `#ifdef` approach. Simpler, no runtime overhead.

### 6B.2: Web SIMD Flag

Add `-msimd128` to Emscripten compile flags in `modules/symphony/SCsub`:
```python
if env["platform"] == "web":
    env.Append(CCFLAGS=["-msimd128"])
```

This enables Clang's auto-vectorizer to emit WebAssembly SIMD instructions for the same loops it vectorizes with NEON/SSE on native.

### 6B.3: Buffer Liveness Analysis (Conditional)

**Trigger**: If tier 4+5 testing shows memory > 100MB at 80+ voices.

**Implementation** (if triggered):
- In the compiler, after topological sort, compute the "live range" of each output buffer (from the operator that writes it to the last operator that reads it)
- Buffers with non-overlapping live ranges can share the same arena memory
- Expected gain: 30-60% memory reduction for audio buffers

**Decision**: Defer implementation until 6C testing confirms it's needed. Document the approach here for reference.

### 6B.4: Loop Unrolling Hints (Conditional)

If the compiler isn't unrolling the 64-iteration inner loops in hot operators:
```cpp
#pragma clang loop unroll_count(8)
for (int32_t i = 0; i < p_num_frames; i++) { ... }
```

**Decision**: Test with `-Rpass=loop-vectorize -Rpass-missed=loop-vectorize` first. Only add pragmas where needed.

### Acceptance Criteria
- [ ] Web builds use micro-block size 32
- [ ] Native builds use micro-block size 64
- [ ] `-msimd128` flag applied to web builds
- [ ] No regression in native performance
- [ ] Buffer liveness analysis implemented IF memory threshold exceeded in 6C

---

## Slice 6C: Voice Tiers 4+5 + Stealing Tests

**Goal**: Test 80 and 120 voices, validate voice stealing logic.

### 6C.1: Voice Stealing Test Scene

New file: `tests/symphony/test_voice_stealing.gd`

**Test cases**:

1. **Max voices limit**: 
   - Set `max_voices = 20`
   - Spawn 30 voices: 10 at priority 90, 10 at priority 50, 10 at priority 10
   - Verify: priority-10 voices get stopped first, priority-90 survive
   - HUD shows which priorities are alive

2. **Budget critical threshold**:
   - Set `critical_threshold = 0.5`, `warning_threshold = 0.3`
   - Spawn tier-3 voices until budget > 50%
   - Verify: voices get culled back to ~30%
   - Verify: high-priority voices survive

3. **RMS tiebreaker**:
   - Spawn 10 voices all at priority 50
   - 5 voices with gain=0.0 (silent), 5 with gain=1.0
   - Set `max_voices = 5`
   - Verify: silent voices get stolen, audible ones survive

4. **No-glitch test**:
   - Spawn 20 voices, set `max_voices = 10`
   - Listen for pops/clicks during stealing
   - (Manual verification — print "STOLEN" when a voice is killed)

### 6C.2: Tier 4+5 Stress Test

Extend `test_stress.gd`:
- Add key `4` for tier 4 (80 voices)
- Add key `5` for tier 5 (120 voices)
- Add memory tracking to HUD (arena bytes per voice)
- Log results to console in CSV format for analysis

### 6C.3: Measurements to Record

| Metric | Tier 4 (80 voices) | Tier 5 (120 voices) |
|--------|--------------------|--------------------|
| Total budget % | | |
| Avg µs per voice | | |
| Peak budget % | | |
| Total arena memory | | |
| Any voice stealing triggered? | | |
| Audible glitches? | | |

### Acceptance Criteria
- [ ] Voice stealing test passes all 4 scenarios
- [ ] 80 voices runs stable for 60+ seconds
- [ ] 120 voices runs stable for 60+ seconds (with stealing if needed)
- [ ] No crashes or memory leaks
- [ ] Results documented

---

## Slice 6A: Web Export & WASM

**Goal**: Build and test Symphony module as WebAssembly, verify performance with 128-sample AudioWorklet buffer.

### Prerequisites
- Slice 6D complete (cross-platform timing)
- Slice 6B complete (web SIMD flag, micro-block size)
- Emscripten SDK installed (version matching Godot 4.x requirements)

### 6A.1: Emscripten Setup

1. Install Emscripten (check Godot docs for required version — likely 3.1.39+)
2. Build Godot web export template: `scons platform=web target=template_release`
3. Verify Symphony module compiles to WASM without errors

### 6A.2: Web-Specific Code Changes

- `symphony_platform_time.h`: Already handled in 6D (`emscripten_get_now()`)
- `SCsub`: `-msimd128` flag (handled in 6B)
- `audio_stream_playback_symphony.cpp`: No `mach_absolute_time` (handled in 6D)
- Verify no other platform-specific code in the module

### 6A.3: Testing

1. Export a test project with the stress test scene
2. Serve with COOP/COEP headers (required for SharedArrayBuffer):
   ```
   Cross-Origin-Opener-Policy: same-origin
   Cross-Origin-Embedder-Policy: require-corp
   ```
3. Test in Chrome/Firefox with 128-sample AudioWorklet buffer
4. Measure budget at 25 voices mixed complexity

### 6A.4: Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| 25 voices mixed, 128-sample buffer | < 60% budget (< 1.74ms) | 2.9ms deadline |
| Per voice average | < 70µs | 128 frames = 2 micro-blocks (or 4 if block=32) |
| No audio dropout | 60 seconds stable | |

### 6A.5: If Targets Not Met

Escalation path:
1. Verify `-msimd128` is active (check WASM binary for SIMD opcodes)
2. Profile with Chrome DevTools → Performance → WebAssembly
3. If specific operators are slow, add manual WASM SIMD intrinsics for those operators only
4. If still too slow, reduce target to 15 voices on web, document limitation

### Acceptance Criteria
- [ ] Godot web export template builds with Symphony module
- [ ] Test scene runs in browser without crash
- [ ] AudioWorklet with 128-sample buffer works
- [ ] 25 voices < 60% budget (or documented limitation)
- [ ] No audio dropout for 60 seconds
- [ ] COOP/COEP headers documented for deployment

---

## Cross-Cutting Concerns

### Voice Stealing — `enforce_voice_limits()` Call Site

Currently `enforce_voice_limits()` exists but **is never called**. It needs to be called from somewhere after all voices have mixed. Options:
- Call it at the end of each `AudioStreamPlaybackSymphony::mix()` — but that's per-voice, not global
- Call it from a process callback on the main thread (polling)
- Hook into Godot's audio server mix cycle

**Decision**: Call from `AudioServer`'s mix callback is ideal but invasive. Simpler: call from `_process()` in the stress test scene for now, and add a proper hook in a later phase. For the voice manager to work in production, we'll need to call `enforce_voice_limits()` from the audio thread after all playbacks have mixed — this requires hooking `AudioServer::mix()` or using a post-mix callback.

**Interim solution for testing**: Add `enforce_voice_limits()` call at the end of each `mix()` call (it's cheap — just iterates the voice list). The last voice to mix in a given audio callback will see the most up-to-date budget numbers. Not perfect but functional.

### State Migration for WavePlayer

WavePlayer has runtime state (read_position, playing flag). Implement `export_state`/`import_state` so hot-swap preserves playback position.

### Editor: Resource Picker Pattern

For WavePlayer's `resource_path` param, the inspector proxy needs to expose it as `PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamWAV"` so Godot shows the resource picker dialog.

---

## Session Checklist

Use this to track progress across sessions:

- [ ] **Slice 6D** — Cross-Platform Timing
- [ ] **Slice 6F** — AUDIO-type GraphInput
- [ ] **Slice 6E** — WavePlayer Node
- [ ] **Slice 6B** — Data-Driven Optimizations
- [ ] **Slice 6C** — Voice Tiers 4+5 + Stealing Tests
- [ ] **Slice 6A** — Web Export & WASM

---

*Last updated: May 30, 2026*
