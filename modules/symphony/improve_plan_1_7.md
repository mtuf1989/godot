# Symphony v1.0 Real-Time Improvement Plan

## Summary

Review outcome: the earlier plan handled allocation and thread safety, but needed stricter controls for transient crossfade CPU, high-rate buffer memory, LOD compilation bursts, silence-processing regressions, and runtime profiling overhead. This revision adds explicit CPU/memory budgets and degradation policies.

Locked requirements:

- Resolve review findings B1–B6 and C1–C12, including M3 regressions.
- Support 22.05, 44.1, 48, and 96 kHz.
- Never allocate, free, lock, compile, query ObjectDB, or mutate dynamic containers on the audio thread.
- Default to bounded state migration plus a 40 ms equal-power crossfade.
- Fall back to a single-graph, 64-sample fade-out/swap/fade-in when CPU headroom is insufficient.
- Limit concurrent crossfades to two on desktop and one on mobile/web.
- Enforce configurable per-graph and global Symphony memory budgets.
- Provide full LOD and feedback serialization/editor support.
- Allow documented API cleanup breaks.

## Memory and Performance Budget

At 96 kHz, approximate maximum arena costs for individual heavy nodes are:

| Node | Maximum runtime memory |
|---|---:|
| DelayLine, 2 seconds | 0.73 MiB |
| PitchShifter, 200 ms | 0.08 MiB |
| FDNReverb, 8 lines plus pre-delay | 0.66 MiB |
| GrainCloud, 10-second live capture | 3.66 MiB |
| PhaseVocoder, FFT 8192 plus COLA table | 0.33 MiB |
| SpectralGate, FFT 8192 | 0.22 MiB |
| WavePlayer current fixed buffer | 0.92 MiB |

A graph swap temporarily requires both old and new graph packages. A 40 ms crossfade extends their coexistence but does not require a third history-copy buffer.

Add configurable limits:

- Per compiled graph: 8 MiB by default.
- Total live, pending, outgoing, and retired graph memory:

  - Desktop: 128 MiB.
  - Mobile: 64 MiB.
  - Web: 32 MiB.

- Shared PCM is counted once in a separate cache total, not once per referencing graph.
- Memory remains charged until the main-thread reclaimer actually destroys a retired package.
- Before compiling, drain retired packages, calculate exact required bytes, reserve the budget, and reject cleanly if the reservation fails.
- Never discard the currently audible graph when a replacement exceeds its budget.
- Expose current/peak arena, pending, retired, and SharedPCM bytes through debug metrics and the editor.
- Show per-tier memory estimates at every supported sample rate in the Symphony editor.

## Implementation Plan

### 1. Restore and measure the baseline

- Correct operator fixtures so bound pointer arrays match descriptor pin counts; Oscillator tests must bind both expected inputs.
- Establish a non-crashing baseline for the existing Symphony tests.
- Split tests into operators, compiler/graph, playback/runtime, spectral, voice/event, and serialization suites.
- Benchmark the existing 10/30/50-node graphs in release mode:

  - Median and p99 mix time.
  - Arena bytes per voice.
  - Main-thread graph compile time.
  - Steady-state and LOD-transition CPU.
  - Peak live graph memory.

- Use long, repeated benchmark runs and compare medians to reduce scheduler noise.
- Gate: tests report failures normally and benchmark results are recorded before architectural changes.

### 2. Add exact resource accounting

- Extend compiler results with exact arena bytes, non-arena bytes, trigger-buffer bytes, route metadata bytes, and total package bytes.
- Replace static arena estimates and 25% headroom with checked alignment-aware calculations.
- Add a central main-thread memory budget service that reserves bytes before compilation and releases them only during main-thread destruction.
- Count active, outgoing, pending, and retired packages.
- Include external PFFFT allocations and unique SharedPCM entries in diagnostics.
- Add compile errors for arithmetic overflow, per-graph limit, and global-budget exhaustion.
- Add operator cost units:

  - A fixed per-sample cost for ordinary operators.
  - An optional parameter-dependent per-block cost for spectral and granular nodes.
  - FFT estimates use `N × log2(N)` scaled by hop frequency.
  - Compiler-injected operators contribute normally.

- Calibrate cost units against measured voice CPU using an EWMA, allowing a new graph’s transient cost to be estimated on the current machine.
- Gate: estimates equal actual allocations exactly, and cost estimates remain conservative for reference graphs.

### 3. Harden compiler and arena lifecycle — B3, B4, C12

- Use Godot’s aligned allocation API with a 32-byte arena base.
- Add checked add/multiply/align helpers and reject overflow before allocation.
- Change `ExtraArenaBytesFunc` to accept sanitized parameters and actual sample rate.
- Give every rate-dependent operator one canonical configuration resolver used by both estimation and construction.
- Make factories transactional:

  - Mark the arena.
  - Allocate and validate backing buffers.
  - Acquire external resources.
  - Placement-construct the operator last.
  - Release external resources and rewind on failure.

- Track planned and successfully constructed operator counts separately.
- Destroy only successfully constructed, non-null operators.
- Check all state, pin, promotion, trigger, routing, and silence-table allocations.
- Add fault-injection tests for null factories, under-reported bytes, external-resource failure, and partial construction.
- Gate: all failures return structured errors without crash, invalid destruction, or leaked budget.

### 4. Introduce immutable prepared graph packages — B1, B2

- Replace raw graph publication with a package containing:

  - Compiled graph and arena.
  - Direct GraphOutput pointer.
  - Compact sorted parameter and trigger route arrays.
  - Node ID, operator type, and structural compatibility fingerprints.
  - Exact memory and estimated-cost totals.
  - Cached immutable LOD and priority metadata.
  - Intrusive retirement link.

- Use flat sorted route arrays instead of per-playback HashMaps. Main-thread control lookup uses binary search; audio never performs name lookup.
- Build and validate the complete package off the audio thread.
- Publish through one atomic pending pointer. Main-thread publication coalesces rapid requests and deletes displaced, never-adopted packages.
- Permit only current, outgoing, and one pending package per playback.
- Never begin a third graph transition while two graphs are executing.

### 5. Implement budget-aware graph transitions

- At the audio block boundary, migrate compatible state using one 256-byte stack buffer.
- Match by node ID, operator type, and structural configuration fingerprint. Skip incompatible or larger histories.
- Estimate additional transition CPU from calibrated cost units.
- Admit the 40 ms equal-power crossfade only when:

  - A global crossfade token is available.
  - Measured global CPU plus estimated incoming cost remains below the warning threshold.
  - The system is below the critical threshold.

- Default concurrent crossfade tokens:

  - Desktop: two.
  - Mobile/web: one.

- Compute equal-power gains using the shared fast periodic sine helper, not per-sample transcendental calls.
- If admission fails, use a bounded low-cost transition:

  1. Fade the old graph to zero over 64 samples.
  2. Migrate bounded state and swap at a block boundary.
  3. Retire the old graph.
  4. Fade the new graph in over 64 samples.

- The fallback never executes two graphs in the same sample period.
- Apply the same scheduler to manual swaps and LOD transitions.
- Under critical CPU load, always use the single-graph fallback.
- Process at most one new LOD compilation per main-thread update to avoid multi-millisecond allocation/memset bursts.
- Gate: a transition cannot raise the measured total above the configured critical threshold without switching to fallback.

### 6. Move all destruction and deferred work off audio — B1, B2

- Push completed, stopped, and superseded graph packages onto an intrusive lock-free retirement stack.
- Drain and destroy them through AudioServer’s main-thread update callback.
- Perform operator cleanup, PFFFT destruction, SharedPCM release, arena freeing, and metadata destruction only there.
- Replace the VoiceManager’s shared pending LOD array with per-playback atomic stop/LOD requests.
- The manager audio callback may only inspect fixed stack snapshots and write atomics.
- Process LOD compilation, stopping, ObjectDB interaction, and SafeList cleanup on the main thread.
- Cache all Resource-derived information before publication.
- Replace `atomic<VoiceMetrics>` aggregates with individual lock-free integer/fixed-point atomics.
- Add a thread-local real-time scope with assertions around every Symphony allocation, free, mutex, ObjectDB access, compile, and dynamic-container mutation.
- Gate: ThreadSanitizer and real-time guard tests report no violations.

### 7. Preserve silence optimization without losing tails — B6

Use three explicit operator behaviors:

- `ALWAYS_PROCESS`: generators, timing/control nodes, and nodes whose output can change without active audio input.
- `STATEFUL_TAIL`: filters, delay, reverb, envelopes, feedback, dynamics, granular, and spectral processors.
- `STATELESS`: gain, mixing, mapping, and other audited zero-in/zero-out processors.

Execution rules:

- Stateless operators skip when all audio predecessors are inactive.
- Stateful-tail operators continue while input is active or their previous state remains active.
- Stateful-tail operators skip only after reporting inactive state.
- Always-process operators never depend on silence propagation.
- FeedbackPath must retain and advance its block history.
- Tail operators use inexpensive activity tracking:

  - Delay lines maintain incremental ring energy rather than scanning buffers.
  - Filters use state magnitude.
  - FDN tracks maximum line state during its existing loop.
  - Envelopes use stage/value.
  - Spectral nodes use pending overlap-add energy.
  - Apply a −120 dB threshold with two-block hysteresis.

- Gate: tails and feedback survive silence while completed inactive tails regain skip performance.

### 8. Correct DSP while controlling added cost — B5, C1–C3, C11

- Centralize sine generation with normalized phase wrapping, quarter-wave folding, and a normalized fifth-order approximation for Oscillator, LFO, FM, and equal-power transitions.
- Replace SVFilter with a topology-preserving state-variable filter; calculate `tan()` only when block-rate cutoff changes.
- Clamp cutoff to `[20 Hz, 0.45 × sample_rate]` and map resonance to Q `0.5–20`.
- Fix ADSR release using the value present at note-off and a precomputed per-sample decrement.
- Refactor FDN:

  - Cache control values at micro-block rate.
  - Recompute RT60 gains only when smoothed controls change.
  - Use `exp2` outside the inner sample loop.
  - Smooth fractional tap position over 20 ms and perform one interpolated read per line.
  - Do not use permanent dual-tap processing during continuous room-size modulation.

- Make static zero-shift PitchShifter emit exact dry input while continuing to maintain history; transition bypass state over 64 samples.
- Gate: correctness tests pass without more than a 5% steady-state regression for unchanged reference graphs.

### 9. Fix RTPC, spectral, and trigger processing — C4–C7

- Replace RTPCEngine’s mutable audio-visible map with a fixed preallocated slot registry.
- Main-thread registration returns a stable handle; audio accesses slots by handle.
- Precompute block smoothing coefficients when rate or callback size changes; the audio callback performs multiply/add only.
- Remove analysis auto-registration and report missing handles.
- Correct PhaseVocoder analysis/synthesis hop phase propagation.
- Track absolute 64-bit ring positions and never read incomplete or overwritten FFT windows.
- Add a precomputed COLA normalization table and include it in exact arena estimates.
- Ensure every PFFFT failure path releases its setup.
- Increase TriggerBuffer capacity to the configured micro-block size: 32 or 64 entries, not an unconditional larger dynamic buffer.
- Give TriggerInput a fixed 64-entry SPSC producer queue.
- Return failure and increment a dropped-trigger counter when full; never silently overwrite.
- Process all WavePlayer triggers at exact sample offsets.
- Gate: RTPC has no audio-thread exponentiation or lookup; spectral and trigger tests pass across rate/block matrices.

### 10. Fix voice stealing and accounting — C8

- Make `SymphonyVoicePool::acquire_slot()` free-only.
- Resolve and validate the selected stream before reserving or replacing a slot.
- At an event cap, select a victim belonging to the same event using its configured mode.
- At the global cap, consider voices whose priority is no greater than the incoming event.
- Implement deterministic oldest, quietest, and farthest selection, with lower importance and slot index as tie-breakers.
- Add a valid-RMS flag and `set_slot_rms()`; unknown RMS values fall back to importance.
- Centralize replacement/release so the victim count decrements exactly once and the incoming count increments exactly once.
- Return `RESULT_STOLEN` with the correct log reason.
- Keep audio-thread manager selection bounded and allocation-free using fixed index arrays.
- Run global budget enforcement every four audio callbacks unless the critical threshold is crossed, reducing repeated scans while keeping response latency bounded.
- Gate: repeated steals and stops cannot leak or underflow event counts.

### 11. Complete LOD and feedback authoring — C9, C10

- Reuse one prefix-aware serializer for the main and LOD graphs.
- Preserve existing `graph/...` properties.
- Add `is_feedback` to each serialized connection and add complete `lod/<tier>/...` graph sections.
- Default older resources to no LOD variants and non-feedback connections.
- Add APIs for adding, duplicating, replacing, querying, and removing two LOD variants.
- Add editor tier selection, tier-specific compile validation, preview, and UndoRedo support.
- Add an UndoRedo-backed feedback toggle with amber dashed edge rendering and an `FB` badge.
- Show estimated arena memory at 22.05–96 kHz and flag tiers exceeding configured budgets.
- Do not precompile all LOD arenas per voice; store descriptions only and compile requested tiers on demand.
- Gate: deterministic `.tres` round trips preserve LODs, frames, quality settings, and feedback metadata.

## API and Type Changes

- `ExtraArenaBytesFunc(params, mix_rate)` replaces the rate-independent callback.
- Operator descriptors gain silence behavior and cost-estimation metadata.
- Compiler results expose exact memory categories and estimated cost units.
- Playback publishes prepared packages rather than raw `CompiledGraph*`.
- `trigger(name, value)` returns `bool`; add dropped-trigger metrics.
- RTPCEngine registration returns a stable handle and cannot auto-create parameters from audio analysis.
- `acquire_slot()` becomes free-only; EventDispatcher owns stealing.
- Add `set_slot_rms()`.
- Remove the requirement to call `process_deferred_lod()` from GDScript.
- Add LOD graph mutation/query methods and LOD/feedback serialization properties.
- Add read-only memory, transition, trigger, spectral-underflow, and retirement metrics.

## Verification and Release Gates

- Test every supported rate with 32- and 64-frame micro-blocks.
- Require zero allocation, free, lock, ObjectDB, Resource, compiler, or dynamic-container activity in audio scopes.
- Run ThreadSanitizer stress for swaps, stops, LOD, parameters, triggers, registration, and teardown.
- Stress global memory limits with many 96 kHz DelayLine, FDN, GrainCloud, WavePlayer, and spectral voices.
- Verify failed reservations preserve the existing audible graph and release all temporary reservations.
- Verify peak package count never exceeds current + outgoing + pending.
- Require retirement queues and memory counters to return to zero after stress teardown.
- Benchmark release builds with profiling sampling enabled and disabled.
- Performance acceptance:

  - No more than 5% median steady-state regression on unchanged 10/30/50-node graphs.
  - No more than 10% p99 regression unless caused by required stateful-tail processing.
  - No callback exceeds the critical budget during admitted crossfades.
  - CPU-denied crossfades use the single-graph fallback.
  - Profiler timing is sampled every eight callbacks.
  - RMS is accumulated using one-in-four sample decimation and an EWMA, avoiding a full-rate second analysis pass.

- DSP acceptance:

  - Sine maximum absolute error ≤ `0.005`.
  - ADSR release timing within one sample.
  - Static zero pitch shift produces sample-identical dry output.
  - No NaN/Inf at parameter extremes.
  - Spectral unity gain within ±0.5 dB after latency alignment.

- Build and test with:

  - `scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no`
  - `bin/godot.macos.editor.arm64 --headless --test '[Symphony]'`

- Release milestones:

  - **M1:** compiler, exact sizing, DSP, silence behavior, and rate matrix pass.
  - **M2:** prepared packages, retirement, manager deferral, transitions, RTPC, spectral, triggers, and voice accounting pass stress tests.
  - **M3:** memory limits, CPU admission/fallback, sanitizers, benchmarks, editor round trips, documentation, and migration notes are complete.

## Assumptions

- GDScript controls run on the main thread.
- Large delay/granular/spectral histories are not migrated; they leave through the transition fade.
- A 256-byte migration buffer is reused sequentially per operator.
- The CPU-safe fallback is preferable to an audio dropout when crossfade headroom is unavailable.
- Hard memory limits are configurable but enabled by default.
- Runtime LOD graphs are compiled on demand, one per main update, to avoid permanent per-voice memory multiplication.
- The separate Game Audio Layer is outside this change, but all required API migrations must be documented before system release.
- The staged review document remains unchanged.
