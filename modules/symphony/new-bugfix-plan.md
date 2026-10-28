# Symphony hardening for real-time games and Web

## Status (2026-10-28)

Uncommitted in `godot` and `game-template`. Do not commit unless asked. Production-readiness stays pending. Full measurements are in `QUALIFICATION.md`.

Sections 2–3 are implemented: ownership, memory budget, scheduler, RTPC, addon lifecycle, offline rendering, and the deterministic tests that cover them.

Section 4 code is in the tree. `wasm_simd` selects the PFFFT SSE1 kernel, callback publication uses 32-bit atomics and `SymphonyPublishedU64`, and the profile harness covers web and desktop baselines plus spectral variants. Both 1,800-second spectral cases honor `SYMPHONY_PROFILE_SECONDS` with no shorter cap. Qualification presets request 100 ms single-thread and 50 ms threaded. Stream playback was already set on Symphony-owned players.

Local gates, run 2026-10-27 on commit `fb1205eaa752`:

| Gate | Result |
|---|---|
| Three Web templates, Emscripten 6.0.12 | Built. SIMD PFFFT objects return size 4 and contain `SSE1`. The scalar object returns size 1 and contains `Scalar`. |
| Native 1,800 s soaks, `template_release` | P99 passed: web-spectral 43 µs, desktop-spectral 110 µs, deadline 2666 µs. Zero-miss failed: 1,475 and 1,185 tight-loop spikes. Symphony bytes stayed flat during each run. Post-teardown totals differ (47,392 then 142,176). |
| Headed Chrome, threaded | Request 50 ms. Startup output-latency estimate 0.0427 s. Ten 5-second hides left `AudioContext` running. Resume backlog unverified. End-to-end latency not measured. |
| Headed Chrome, single-thread | Request 100 ms. Startup output-latency estimate 0.0853 s, then the page stopped servicing the browser. Resume cycles were not run. |
| Symbolized ASan/UBSan | 364/364 passed. 0 ASan reports. 417 UBSan reports: `alignas(64)` objects allocated with `memnew`. |
| Symbolized TSan | 364/364 passed. 2 warnings: the B0 test helper reads `tempo_count` during `_audio_restart`; upstream `AudioDriverDummy::set_mix_rate` races its mixer thread. |

Still open: Firefox, Safari measurements, the single-thread page stall and its resume cycles, a Web 1,800-second soak, in-browser mix and underrun counts, end-to-end latency, Android Chrome, iOS Safari, Linux RTSan, and designer listening.

`rope_chain` level: the gain param that was 0.4 is now 0.25, and its modulation pin overrides that param. The level that reaches the output is MapRange id 19 (`material` → final gain pin); its `out_min` / `out_max` are scaled by 0.65.

Leave these alone unless the next session is still inside sections 2–3:

- Clip identity is context handle (pool slot + reuse generation) plus musical identity (playback epoch + phrase generation + track id + clip event id assigned from the pre-sort ordinal). Activation requires both, and the scheduled tick.
- Controller generation map is capped at 8. Dialogue `queue_limit = -1` means project setting `audio/symphony/dialogue_queue_limit` (default 32). Offline chunks are at most 4096 stereo frames at 48 kHz.

## 1. Goals and operating limits

Fix all eleven review findings across the native module and addon while preserving procedural synthesis, adaptive music, recorded phrases, spatial acoustics, RTPCs, dialogue, and effects.

Support **threaded and single-threaded Web exports**, including desktop browsers and midrange mobile browsers. Feature availability remains consistent; concurrent capacity and buffering may differ.

Use these automatic memory defaults:

| Platform | Default Symphony budget |
|---|---:|
| Desktop native | 128 MiB |
| Mobile native | 64 MiB |
| Web, including mobile browsers | 32 MiB |

- Keep memory limits separate from voice and DSP limits. Increasing memory must not automatically increase callback workload.
- Keep Symphony mixing free of heap allocation, deallocation, blocking locks, compilation, decoding, FFT setup, and logging.
- Perform preparation and reclamation on the main thread. Single-threaded Web must work without background workers.
- Preserve fixed queue and pool capacities. Fix exhausted-capacity bugs through correct reuse rather than enlarging buffers.
- Reject operations that exceed capacity with explicit errors while preserving existing playback.

Implement in this order: regression fixtures → ownership and memory → scheduler → addon lifecycle → offline rendering → Web and release qualification.

## 2. Native ownership, memory, and scheduling

### PCM and stretch-context lifetime

- Give each prepared stretch context its own main-thread cache lease. Replacing a registry entry releases only the registry’s lease; submitted phrases retain their original PCM.
- New submissions use the replacement version. Existing prepared and active contexts finish using their retained version.
- Charge every unique retained version once. Replacement must reserve space before publishing; insufficient memory returns `ERR_OUT_OF_MEMORY` and leaves the previous registration intact.
- Transfer prepared entries into active ownership when claimed, removing their prepared-table records immediately.
- Remove every audio-side reference before publishing a context as returned. The audio thread must never inspect that context afterward.
- Reclaim leases and reset context fields only after the main thread observes the completed ownership transfer.
- Match clip commands using context generation and musical identity so stale commands cannot claim a recycled context.
- Preallocate scratch capacity for the supported sample-rate, tempo, and transpose ranges. Reuse it without growth during processing.

### Configurable memory budget

Add:

- Project setting `audio/symphony/memory_budget_mib`, where `0` selects the platform default.
- Main-thread `set_memory_budget_mib(value) -> Error` and `get_memory_budget_mib() -> int` on `SymphonyVoiceManager`, with `AudioManager` wrappers.
- `0` restores automatic selection; negative or overflowing values return `ERR_INVALID_PARAMETER`.
- Raising the limit changes admission headroom without allocating the allowance immediately. Lowering below committed usage returns `ERR_BUSY` without changing playback or the active limit.
- Document how a game settings menu can call this API and persist the preference. Do not add game-specific settings UI.

Expand accounting to include graph arenas, parked and recycled arenas, unique PCM versions, playback tables, stretch/FFT working storage, effect buffers, and preparation scratch. Reserve preparation peaks before allocation and release reservations on every failure path.

Use checked footprint calculations and capacity accounting for third-party buffers, verified against actual allocated capacities in debug tests. Report current usage, peak usage, retained PCM, reusable storage, preparation memory, and rejected admissions through existing debug metrics.

The budget covers Symphony-managed allocation payloads; Godot resources, browser audio buffers, allocator overhead, and total WASM memory remain separately reported.

### Scheduler and RTPC fixes

- Publish a phrase slot’s queued state **before** publishing its command. After publication, the producer relinquishes ownership until retirement.
- Parse, stamp generations, and sort phrases on the main thread. Audio adoption copies only valid events into preallocated storage; remove the large temporary phrase from the callback stack.
- Bound command consumption to the commands present at callback entry, capped by ring capacity.
- Compact consumed carry entries before admitting more note-offs. Retain the 512-entry simultaneous capacity and existing explicit overflow behavior.
- Make RTPC frame-count and coefficient caches exclusively audio-owned. Each callback takes one published parameter-count snapshot, detects new registrations, computes their coefficients, and processes that same snapshot.
- Bound controller generation bookkeeping and failed-submission retries so long sessions do not accumulate history indefinitely.

## 3. Addon behavior and offline rendering

### Music transitions

- Capture outgoing players before starting incoming playback; fade only those outgoing players.
- Preallocate a second synchronized player so layered states can crossfade correctly.
- Track transition generations and owned tweens. Invalidate old work on stop, reload, or a superseding transition; guard timer callbacks because `SceneTreeTimer` cannot be cancelled.
- Apply the same identity checks to delayed procedural-controller cleanup.
- When overlapping states cannot fit the memory budget, use fade-through-silence and wait for retirement without blocking. Report failure if the incoming state still cannot be prepared.
- Detach the stopped session’s BeatClock immediately. Its later cleanup must not alter a replacement session.

### Dialogue lifecycle

- Assign each accepted line and player activation an internal identity. Fade completion acts only on that activation.
- Emit completion exactly once for natural finish, interruption, and manual stop, including when another line is queued.
- Attach `play_from_dialogue_line()` completion callbacks to their accepted line rather than the next global completion signal.
- Track overlay completion and ducking across all owned voice players.
- Preserve the four-player pool. Bound queued lines to 32 by default, expose the queue limit, and report admission failure.
- Make `queue_line()` return whether the line was accepted; existing callers may ignore the return value.

### Offline rendering and content

- Add `AudioStreamSymphonyMusic.instantiate_offline_playback(seed = 0)`. Prepare its transport, instruments, and clips at **48 kHz before accepting phrases**.
- Keep live playback construction device-rate aware. `render_offline()` must refuse live-mode playbacks and must never retag an already prepared playback’s rate.
- Store canonical clip PCM at 48 kHz. Share rate-specific live views on demand, with every additional view included in memory admission.
- Update native tests, documentation, and the addon WAV helper to use the offline factory.
- Feed and render offline phrases incrementally rather than queuing the entire score. Write WAV exports in bounded chunks; reserve returned-array memory for direct `render_offline()` calls.
- Retune `rope_chain` through static output gain, initially reducing its final gain from `0.4` to `0.25`. Retain the existing peak and audibility assertions; avoid adding per-voice limiter cost.
- Repair stale authoring examples and regenerate their mirrors without weakening tests.

## 4. Web parity and performance qualification

Use Godot **Stream playback** throughout Symphony-owned players. Godot’s Sample path omits procedural generation and audio effects; Stream preserves those features with additional latency. [Godot Web audio documentation](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_web.html#audio-playback)

- Build matched release templates with `threads=yes` and `threads=no`, using the repository’s pinned Emscripten toolchain.
- Respect `wasm_simd` in Symphony and PFFFT build flags. Include a scalar build smoke test.
- Audit callback atomics for WASM support; replace aggregate atomics that introduce hidden locking with scalar publication or bounded snapshots.
- Use separate qualification export profiles: request **100 ms buffering for single-threaded Web** and **50 ms for threaded Web**. Report actual latency because browsers may round or add buffering.
- Test actual buffer lengths rather than assuming every callback contains 128 frames. [AudioWorklet processing guidance](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorkletProcessor/process)
- Load imported recording assets through Godot’s resource loader; reserve raw-file loading for external files.
- Test audio unlock, pause/resume, tab suspension, scene changes, and repeated start/stop. Resume must not replay an unbounded backlog.
- Keep every operator and subsystem available in both Web modes. Overload handling uses existing priority, LOD, virtualization, admission, and transition fallback policies.

Replace the sine-only performance fixture with mixed workloads:

| Qualification workload | SFX voices | Synth notes | Stretched clips | Spatial voices* | Reverbs |
|---|---:|---:|---:|---:|---:|
| Web/mobile baseline | 8 | 8 | 1 | 4 | 1 |
| Native desktop baseline | 32 | 32 | 2 | 8 | 2 |

\*Spatial voices are included in the SFX count. Exercise spectral operators in an additional workload.

Measure callback P50/P95/P99, deadline misses, underruns, main-thread preparation time, memory peaks, and admission failures. Initial gates are P99 mixing below 50% of the actual render deadline, steady main-thread audio updates below 2 ms P99 at 60 FPS, and no underruns during the controlled foreground baseline after warm-up. Larger workloads are capacity measurements, not assumed guarantees.

Use fixed counters and bounded sampling; avoid per-block messages or logs.

## 5. Regression tests, release gates, and delivery

Add deterministic regressions for:

- Clip replacement during preparation and playback, including budget rejection and final lease release.
- More than 1,000 sequential clip plays with continuing audible output.
- Context return racing immediate reuse, and forced phrase-publication interleavings.
- More than 10,000 phrase boundaries with correctly timed carried note-offs.
- Concurrent RTPC registration, smoothing changes, and callback-size changes.
- Single-stream and layered crossfades, rapid state requests, stop/reload, and stale fade completion.
- Queued and overlapping dialogue, with per-line completion exactly once.
- Identical seeded offline musical timing at 44.1/48 kHz device rates, plus long chunked WAV exports.
- Platform defaults, runtime budget increases, rejected decreases, allocation rollback, and teardown accounting.
- Existing preset level limits and corrected authoring examples.

Release gates:

- Rebuild and pass the complete native and addon suites.
- Run symbolized ASan/UBSan and TSan stress. Require zero Symphony memory errors and races; explicitly identify any remaining upstream reports.
- Add Linux RTSan coverage that includes third-party allocation paths, beyond Godot’s allocation counter.
- Extend both existing Web build jobs with browser playback tests. Automate desktop Chromium, Firefox, and WebKit checks; qualify real Android Chrome and iOS Safari on representative midrange devices.
- Run 30-minute native and Web mixed-workload soaks. Require bounded memory after warm-up and return to the expected baseline after teardown and reclamation.
- Perform listening checks for transitions, tails, spatial effects, clipping, and Web latency.

Deliver fixes as separately reviewable changes and update the engine/addon build pin together. `QUALIFICATION.md` now records the 2026-10-27 measurements. Production-readiness stays pending until the sanitizer, Web, sustained-performance, and listening gates pass.