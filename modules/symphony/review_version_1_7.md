# Symphony Module — Technical Review v1.7

**Date:** 2026-08-12
**Verdict:** No-go for production. Architecture is directionally good; implementations violate its own guarantees in ways that crash, corrupt state, or produce wrong audio.

---

## Release Blockers

### B1. Graph destruction on the audio thread

`_finalize_stop()` and the LOD crossfade completion path call `memdelete(graph)` inside `mix()`. `CompiledGraph::destroy()` runs operator `cleanup()` (which can take the `SharedPCMCache` mutex), calls destructors, frees the arena, and calls `memdelete_arr` four times.

`SymphonyVoiceManager::enforce_voice_limits()` — registered via `add_mix_callback` — constructs a `Vector<ObjectID>` with `push_back`, calls `ObjectDB::get_instance()` (which acquires a **global spinlock** at `core/object/object.h:919`), and calls `stop()` on victims.

`rebuild_routing_tables()` is called from `mix()` during hot-swap and performs `HashMap::clear()` + `HashMap::insert()` (heap allocation) plus `dynamic_cast` per operator.

All of this breaks the zero-allocation/non-blocking audio-thread contract and can cause glitches or priority inversion.

**Files:** `stream/audio_stream_playback_symphony.cpp:104,160,280`, `core/symphony_voice_manager.cpp:238-344`, `core/shared_pcm_cache.cpp:53`

### B2. Hot-swap data races and undefined behavior

`swap_graph()` reads `current_graph->operators[i]->export_state()` on the main thread while the audio thread executes or destroys that graph. Plain concurrent non-atomic access to non-trivial state is undefined behavior, not merely a stale read.

`set_parameter()` and `trigger()` read `parameter_map`/`trigger_map` from the game thread while `rebuild_routing_tables()` in `mix()` clears and rewrites them. Profiling fields (`last_mix_time_us`, `last_rms`, `last_frame_count`) are plain floats written by the audio thread and read by game-thread getters.

**Files:** `stream/audio_stream_playback_symphony.cpp:192`, `stream/audio_stream_playback_symphony.h:37`

### B3. Compiler crash on operator creation failure

The compiler sets `compiled->operator_count = node_count` before constructing operators in Phase 7. If any `create_fn` returns `nullptr`, it calls `memdelete(compiled)` which triggers `destroy()` — dereferencing every operator slot including unconstructed nulls.

Many `arena.alloc()` results are used without null checks: FDN's `delay_memory`, `pre_delay_memory`, and `mem` pointer; multiple per-pin buffer allocations.

**Files:** `core/symphony_graph_compiler.cpp:448`, `core/symphony_compiled_graph.h:113`

### B4. 96 kHz exceeds arena budgets

The stream property exposes `"22050,96000,1"` for mix_rate, but `extra_arena_bytes` is computed for 48 kHz:

| Operator | Budget (floats) | Need at 96 kHz (floats) | Overflow |
|----------|----------------|------------------------|----------|
| DelayLine (2000 ms) | 96,004 | 192,004 | 2× |
| PitchShifter (200 ms) | 9,604 | 19,204 | 2× |
| GrainCloud (2 s) | 96,000 | 192,000 | 2× |
| FDN (8-line, 200 ms + 200 ms pre) | 100,000 | 172,809 | 1.7× |

An arena overrun returns null; the operator calls `memset`/`new` on it → crash.

**Files:** `nodes/delay/symphony_delay_line.h:104`, `nodes/delay/symphony_pitch_shifter.h:191`, `nodes/synthesis/symphony_grain_cloud.h:518-522`, `nodes/delay/symphony_fdn_reverb.h:248`

### B5. Oscillator is mathematically wrong

`fast_sine(phase)` maps `[0,1)` to `x = 2·phase−1` then evaluates a 5th-order polynomial of `sin(πx/2)`. This produces **−cos(π·phase)** — a half-cosine ramp from −1 to +1 with a full-scale discontinuity at phase wrap.

`fast_sine(0)` ≈ −1.005, not 0. The same broken function is duplicated in `symphony_lfo.h:25` and `symphony_fm_oscillator.h:48`.

The unit test expects `out_buf[0] == Approx(0.0)`, but its fixture passes `void *inputs[] = { nullptr }` (1 element) to `bind_pins` which reads indices `[0]` and `[1]` — out-of-bounds access prevents the test from ever reaching the assertion.

**Files:** `nodes/generators/symphony_oscillator.h:37`, `nodes/generators/symphony_lfo.h:25`, `nodes/generators/symphony_fm_oscillator.h:48`, `tests/modules/test_symphony_operators.cpp:41`

### B6. Silence propagation removes effect tails

When all audio inputs of a skippable operator are inactive, `CompiledGraph::execute()` zeroes its output and skips `execute()`. The `skippable` field defaults to 1; only `Generators/IO/Timing/Synthesis` categories are exempted.

This means Delay, Reverb, Filters, Compressor, and Spectral operators are silenced the instant their source stops — reverb tails cut off, filter ringing disappears, delay echoes vanish. Additionally, zeroing skipped operators' output buffers destroys the implicit one-block delay that feedback edges rely on.

Only ADSR and Gain currently report `activity = 0`, so in the canonical `Osc → ADSR → Reverb` topology, the reverb is hard-zeroed when the envelope reaches IDLE.

**Files:** `core/symphony_compiled_graph.h:69`, `core/symphony_graph_compiler.cpp:554`, `core/symphony_operator.h:22`

---

## Important Correctness Defects

### C1. SVFilter cutoff hard-capped at ~fs/6

`if (f > 1.0f) f = 1.0f` clamps `2·sin(π·cutoff/fs)` to 1.0. At 48 kHz, any cutoff above ~8 kHz saturates to the same effective frequency. The Chamberlin stability bound is `f < 2`; a proper clamp at 1.8 or switching to the trapezoidal SVF would allow full-range sweeps.

**File:** `nodes/filters/symphony_sv_filter.h:45-46`

### C2. ADSR release time is wrong outside sustain

`release_inc = sustain_level / (release_time * mix_rate)` is fixed at construction. A note-off during attack (envelope at 0.95, sustain at 0.2) takes 4.75× the configured release time. Standard fix: recompute `release_inc = envelope_value / (release_time * mix_rate)` at note-off.

**File:** `nodes/envelopes/symphony_adsr.h:74`

### C3. FDN recomputes integer delay lengths every micro-block

`_update_delay_lengths(room_size)` is called inside `execute()` at `symphony_fdn_reverb.h:121`. Integer truncation of `base_delay_samples / ratios[l]` means tap positions jump discontinuously when `room_size` is modulated — producing zipper clicks every ~1.3 ms. Additionally, 4–8 `Math::pow()` calls per micro-block add non-trivial cost.

**File:** `nodes/delay/symphony_fdn_reverb.h:121,135`

### C4. RTPCEngine calls `powf()` on the audio thread

`smooth_all()` runs per mix callback via `add_mix_callback`. For every active parameter, it calls `powf(1.0f - param.smooth_coeff, (float)p_num_frames)`. Up to 128 `powf` calls per mix cycle — non-deterministic latency, platform-dependent RT safety.

**File:** `runtime/rtpc_engine.cpp:68`

### C5. RTPCEngine `set_analysis_value()` auto-registers via HashMap insert

The else branch calls `register_analysis_output()` which does a `HashMap::insert` (heap allocation). The function is documented for audio-thread operators. While no in-tree operator currently calls it, the path exists and the comment encourages it.

**File:** `runtime/rtpc_engine.cpp:194-204`

### C6. Spectral processing algorithmic errors

- **PhaseVocoder** uses `expected_phase_advance = TAU * hop_size / fft_size` (synthesis hop) for unwrapping, but the analysis hop varies with time stretch. Phase coherence breaks at any ratio ≠ 1.0.
- **PhaseVocoder** `analysis_pos` is never clamped against `input_samples_fed` — can read positions not yet filled.
- **Both processors** apply a Hann window at analysis and again at synthesis. At the default overlap=4, Hann²'s COLA sum is 1.5 (+3.5 dB). Nominal bypass is not unity gain.

**Files:** `nodes/spectral/symphony_phase_vocoder.h:87,100`, `nodes/spectral/symphony_spectral_gate.h:125,343`

### C7. Trigger delivery loses events

`SymphonyTriggerInput` uses separate `pending_value`/`pending_flag` atomics. A `fire()` between the consumer's `load(flag)` and `store(false)` is silently overwritten. Fixed-size `TriggerBuffer` silently discards overflow. `WavePlayer` ignores `sample_offset` — all triggers fire at sample 0 regardless of timing.

**Files:** `nodes/io/symphony_trigger_input.h:32`, `nodes/generators/symphony_wave_player.h:77`

### C8. Voice management accounting is broken

- `steal_lowest_importance()` sets `state = VOICE_FREE` without decrementing `event_voice_counts` → per-event counts leak monotonically until all plays are rejected.
- `SoundEvent::steal_mode` (Oldest/Quietest/Farthest) is declared and bound but never read — only `steal_lowest_importance()` is called.
- `RESULT_STOLEN` is an enum constant that is bound and switched-on but never assigned to any result.

**Files:** `runtime/voice_manager.cpp:108,155`, `runtime/event_dispatcher.cpp:79,83`

### C9. Feedback edges are not serialized

`is_feedback` is used by the compiler to break cycles but is missing from `_get_property_list`/`_get`/`_set`. Saving a `.tres` with feedback connections discards the flag; reloading produces a cycle error. The editor plugin has zero references to `feedback`.

**Files:** `stream/audio_stream_symphony.cpp:53`, `core/symphony_graph_description.h:23`

### C10. LOD graphs cannot be authored

`lod_graphs` is a private `Vector<GraphDescription>` with no setter, no bound method, and no stored property. The user guide documents LOD as a working feature and advises "Always define LOD 1/LOD 2 variants" — but there is no public API to do so.

**File:** `stream/audio_stream_symphony.h:19-21`

### C11. PitchShifter comb filtering at 0 semitones

At zero shift, both read pointers are far from the write head and both get `crossfade = 1.0`, normalized to 0.5 each. The output becomes the average of two time-offset copies of the input — comb filtering, not unity passthrough.

**File:** `nodes/delay/symphony_pitch_shifter.h:109-128`

### C12. Arena base alignment not guaranteed

`ArenaAllocator::init` uses `memalloc()` (8-byte aligned on most platforms). Internal offsets are 32-byte aligned relative to an unaligned base, so absolute 32-byte alignment (needed for SIMD) is not guaranteed.

**File:** `core/symphony_arena_allocator.h:17`

---

## Test Coverage

- 13 unit tests exist in `tests/modules/test_symphony_operators.cpp`, covering 6 operator families.
- The oscillator test fixture has an out-of-bounds access that prevents execution.
- No automated tests for: graph compiler, hot-swap, serialization round-trip, LOD, voice/event, spectral, high-sample-rate, concurrency, or silence propagation.
- The test runner crashed during engine initialization; Symphony cases have never executed in CI.

---

## Implementation Plan

Ordered by cost/benefit — cheap audibility fixes first, expensive architectural work last.

### Phase 1 — Make it sound correct (1–2 days)

**Goal:** Every operator produces mathematically correct output. Enables meaningful listening tests.

| # | Task | Files |
|---|------|-------|
| 1.1 | Fix `fast_sine`: shift phase by 0.25 so `fast_sine(0) = 0`, or rewrite as proper `sin(2π·phase)` polynomial. Propagate to LFO and FM. | `symphony_oscillator.h`, `symphony_lfo.h`, `symphony_fm_oscillator.h` |
| 1.2 | Fix test fixture: `void *inputs[] = { nullptr, nullptr }`. Verify all 13 tests pass. | `test_symphony_operators.cpp` |
| 1.3 | SVFilter: raise clamp to `1.85f` (or implement trapezoidal SVF). | `symphony_sv_filter.h` |
| 1.4 | ADSR: recompute `release_inc = envelope_value / (release_time * mix_rate)` at note-off transition. | `symphony_adsr.h` |
| 1.5 | FDN: move `_update_delay_lengths()` to construction and to a `set_room_size()` setter; remove from `execute()`. Replace per-sample `Math::pow` with precomputed `line_gains[]`. | `symphony_fdn_reverb.h` |
| 1.6 | PitchShifter: when `shift_semitones ≈ 0`, bypass crossfade and output pointer A at gain 1.0. | `symphony_pitch_shifter.h` |
| 1.7 | RTPCEngine: precompute `alpha` at registration time for the known block size; remove `powf` from `smooth_all()`. | `rtpc_engine.cpp` |

### Phase 2 — Compiler hardening and sample-rate safety (1 day)

**Goal:** No crash from malformed graphs or high sample rates.

| # | Task | Files |
|---|------|-------|
| 2.1 | Track `constructed_operator_count` in Phase 7; `destroy()` only iterates up to that count. | `symphony_graph_compiler.cpp`, `symphony_compiled_graph.h` |
| 2.2 | Check every `arena.alloc()` result; return compile error on null. | `symphony_graph_compiler.cpp`, all operator `create()` functions |
| 2.3 | Make `extra_arena_bytes_fn` mandatory for DelayLine/PitchShifter/GrainCloud/FDN. Compute from `p_params` and `p_mix_rate`. Remove fixed `extra_arena_bytes`. | `symphony_delay_line.h`, `symphony_pitch_shifter.h`, `symphony_grain_cloud.h`, `symphony_fdn_reverb.h` |
| 2.4 | Decision: either clamp `mix_rate` property to 48000, or audit all operators for rate-dependent sizing. Document the choice. | `audio_stream_symphony.cpp` |
| 2.5 | `ArenaAllocator::init`: use `memalloc` with capacity + 31, then align `base` to 32. Or use Godot's aligned allocator if available. | `symphony_arena_allocator.h` |

### Phase 3 — Silence propagation fix (0.5 day)

**Goal:** Effect tails and feedback loops survive source silence.

| # | Task | Files |
|---|------|-------|
| 3.1 | Add `uint8_t has_internal_state = 0` to `SymphonyOperator`. Set to 1 for Delay, FDN, Filters, Compressor, Spectral, FeedbackPath. Override `skippable` logic: `if (has_internal_state) skippable = 0`. | `symphony_operator.h`, `symphony_graph_compiler.cpp` |
| 3.2 | Alternatively: add a per-operator `tail_samples` counter. Skip only when `tail_samples == 0`. Operators decrement when their output energy is below threshold. | (deferred to v2 if simpler approach in 3.1 suffices) |

### Phase 4 — Serialization and authoring (1 day)

**Goal:** Graphs survive save/load round-trips; LOD and feedback are authorable.

| # | Task | Files |
|---|------|-------|
| 4.1 | Add `is_feedback` to connection serialization in `_get_property_list`/`_get`/`_set`. | `audio_stream_symphony.cpp` |
| 4.2 | Add `lod_graphs` serialization: stored as `lod/0/node_count`, `lod/0/nodes/...`, etc. Add `set_lod_graph(tier, GraphDescription)` and `get_lod_graph(tier)` bindings. | `audio_stream_symphony.cpp`, `audio_stream_symphony.h` |
| 4.3 | Editor plugin: add feedback toggle on connections and LOD graph tab. | `editor/symphony_editor_plugin.cpp` |

### Phase 5 — Voice and event system (0.5 day)

**Goal:** Stealing, instance limits, and triggers work correctly.

| # | Task | Files |
|---|------|-------|
| 5.1 | `steal_lowest_importance()`: call `on_voice_stopped(event_id)` to decrement `event_voice_counts` before freeing the slot. | `voice_manager.cpp`, `event_dispatcher.cpp` |
| 5.2 | Implement `steal_mode` dispatch: route through `SoundEvent::get_steal_mode()` to select oldest/quietest/farthest logic. Assign `RESULT_STOLEN` on success. | `event_dispatcher.cpp` |
| 5.3 | `SymphonyTriggerInput`: replace two atomics with a single `std::atomic<uint64_t>` packing value + generation counter, or use a lock-free SPSC slot. | `symphony_trigger_input.h` |
| 5.4 | `WavePlayer`: respect `sample_offset` from trigger events. | `symphony_wave_player.h` |

### Phase 6 — Spectral correction (1 day)

**Goal:** PhaseVocoder and SpectralGate produce correct output at all stretch ratios.

| # | Task | Files |
|---|------|-------|
| 6.1 | PhaseVocoder: use actual analysis hop `(1/time_stretch) * hop_size` for `expected_phase_advance`. | `symphony_phase_vocoder.h` |
| 6.2 | Clamp `analysis_pos` to `[0, input_samples_fed - fft_size]`. | `symphony_phase_vocoder.h` |
| 6.3 | Both processors: normalize synthesis window by the COLA factor `sum(window²)` / hop_size, or use sqrt-Hann for both analysis and synthesis. | `symphony_phase_vocoder.h`, `symphony_spectral_gate.h` |
| 6.4 | Add bypass/impulse golden tests that verify unity gain at stretch=1.0 and correct time scaling at stretch=2.0. | `test_symphony_operators.cpp` (new test file) |

### Phase 7 — Audio-thread safety rearchitecture (3–5 days)

**Goal:** Zero allocation, zero locks, zero blocking on the audio thread.

| # | Task | Files |
|---|------|-------|
| 7.1 | Implement a **retirement queue**: audio thread pushes old graphs to a lock-free SPSC queue; main thread drains and deletes them in `_process()`. Remove all `memdelete` from `mix()` and `enforce_voice_limits()`. | New: `core/symphony_retirement_queue.h`, modify `audio_stream_playback_symphony.cpp`, `symphony_voice_manager.cpp` |
| 7.2 | `rebuild_routing_tables()`: move to main thread. Publish routing tables as an immutable snapshot via the same `pending_graph` atomic slot (bundle routing with the graph). | `audio_stream_playback_symphony.cpp/.h` |
| 7.3 | `swap_graph()` state migration: have the audio thread export state into a lock-free mailbox at the end of each mix cycle (lazy snapshot). Main thread reads the mailbox, builds new graph, imports state, publishes via atomic. | `audio_stream_playback_symphony.cpp` |
| 7.4 | `SymphonyVoiceManager`: replace `Vector<ObjectID>` with a fixed-size array. Replace `ObjectDB::get_instance()` calls with SafeList iteration that avoids the spinlock. Move `stop()` calls to a deferred main-thread pass (similar to LOD). | `core/symphony_voice_manager.cpp` |
| 7.5 | `RTPCEngine::set_analysis_value()`: remove the auto-register path; require pre-registration. Add `ERR_FAIL_COND` if key not found. | `runtime/rtpc_engine.cpp` |
| 7.6 | Make profiling fields (`last_mix_time_us`, `last_rms`) atomic or publish via a struct snapshot. | `audio_stream_playback_symphony.h` |

### Phase 8 — Test infrastructure (2 days, parallel with Phase 7)

**Goal:** Automated regression gates prevent reintroduction.

| # | Task |
|---|------|
| 8.1 | Fix test runner initialization crash (disable Embree/OpenXR in test build or mock them). Verify all 13 existing tests pass. |
| 8.2 | Add compiler tests: valid graph, cycle detection, missing required input, feedback edge, arena overflow, null factory. |
| 8.3 | Add DSP golden tests: oscillator (sine/saw/square), ADSR envelope shape, delay impulse response, FDN RT60, SVFilter frequency response, spectral bypass. |
| 8.4 | Add concurrency tests: hot-swap under load, trigger fire during mix, parameter set during mix. Run under TSan. |
| 8.5 | Add serialization round-trip tests: save `.tres` → reload → compile → compare graph output. |
| 8.6 | Add sample-rate matrix: run all golden tests at 22050/44100/48000/96000. |
| 8.7 | Add audio-thread allocation detector (override malloc, assert not called from audio thread context). |

---

## Milestone Gates

| Milestone | Criteria | Enables |
|-----------|----------|---------|
| **M1: Sounds correct** | Phases 1–3 complete, golden tests pass at 48 kHz | Listening evaluation, sound design iteration |
| **M2: Robust** | Phases 2, 4–6 complete, serialization round-trips, no crash on fuzz | Editor workflow, asset pipeline |
| **M3: Ship-ready** | Phases 7–8 complete, TSan clean, no audio-thread allocations detected | Production gameplay integration |

---

## Verification Performed

- macOS arm64 editor build: **succeeds**
- Phase 3 scene headless graph compilation: **succeeds**
- `tests=yes` build: **compiles** but test runner crashes before executing Symphony cases
- No source files were modified during this review
- All claims verified against source code at current HEAD
