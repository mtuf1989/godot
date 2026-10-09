# Symphony: fix verified bugs and adopt the talk2 insights (phased plan)

> **This file is the master roadmap.** It is paired with [`proc-music-plan.md`](proc-music-plan.md), and together they form one plan. Phases P0–P10 are technical work; milestones N0–N3, G1, G2 and V1 are procedural music. The order of all of them is set in **§Unified roadmap** below. The music plan defers to this file for every shared prerequisite.

## Progress (update every phase commit)

| Field | Value |
|---|---|
| **As of** | 2026-10-09 |
| **Branch** | `master` (ahead of `origin/master`; not pushed) |
| **Done** | **P0–P8a**, **P10**, music **N1** + **N2**; **S0 / N0** closed. SFX lanes for S1–S4 prerequisites landed early (P5–P7, P8a). |
| **Next** | **S3** — music **N3** (recorded phrases) ‖ composer **G1**. Parallel optional: **P8b**, **P9**. |
| **Stage** | S3 Recordings + composer (S2 instruments gate ✅) |
| **C++ tests** | 289/289 `*Symphony*` green on `bin/godot.macos.editor.arm64` |
| **TSan** | `.san` after P1b: **1** warning (same as P0 `DynamicRangeHigh`); new Symphony-frame races = 0 |

### Commits

| Phase | Commit | Notes |
|---|---|---|
| P0 | `26b1e645be` | Shared helpers (`render_graph`, `tone_level_db`, `alias_ratio_db`); TSan rebuild + baseline in `MIGRATION.md` |
| P1 | `d739292608` | Single-owner lifetime; denormal guard; RTPC atomics; LOD hysteresis; `test_symphony_lifetime.cpp` |
| P1b | `c5d38b563b` | Promotion order, atomic-word SeqLock, per-voice seeds, `frames_rendered`, unresampled `render_offline` |
| S0 gate | `81e362e111` (+ game-template `9ed46af4`) | TSan re-baseline (= P0 warning count); T3; parked-package docs; torch_fire GDScript N1 |
| P3 | `dd19939a3a` | HIIR saturator OS, u-domain ADAA, waveshaper 3-way ADAA + foldback, FM Carson clamp, staircase/rate LPF, FormantOsc blend, DC R(sr), Biquad τ=5ms |
| P4 | `1a84c38e46` | Rate policy helper; SFX mix_rate 48k default + clamp; WavePlayer loop-aware Hermite; OnePole/SVF open bypass; osc dt∈[0,0.49] |
| P2 | `554859a7d4` (+ game-template `b9e17326`) | N2 3D filter/max_db; N3 WavePlayer multi-format decode; `test_symphony_p2_wave.cpp` |
| N1 | `5aeed32bce` | Music transport/scheduler; BeatClock rewrite; `trigger_at`; Interactive position; `test_symphony_music_n1.cpp` |
| P5 | `78ea215b35` | PhaseVocoder pitch-only; SpectralGate; GrainCloud upgrade |
| P6 | `d6bfa4adfc` (+ game-template `8b1932bb`) | Shared FDN/ER cores; `AudioEffectSymphonyReverb`; ASP3D DRR; reverb pool SlotParams |
| P7 | `1c50d03cf0` (+ game-template `29727941`) | Clock t=0 + bpm/phase; asymmetric RTPC; MusicStateGraph tempo validate |
| P8a | `c0779edf40` (+ game-template `266c9f3d`) | `SoundEvent.tier` / VITAL; pool importance key; N13; removed stream `voice_priority` |
| P10 | `94748f2e00` (+ game-template `ed64b879`) | Resettable-state contract; ModalBank `pitch_ratio`/`root_hz`; migrate 11 PitchShifter presets |
| N2 | `db7f7ecb05` | `MusicNoteInput`, note pools, protected capacity, multi-track phrase + 44.1/48k tests |

### P1 leftovers (closed — optional coverage only)

S0 gate items are done (TSan re-baseline, torch_fire GDScript N1, parked-package docs, lifetime T3). Remaining lifetime cases **T2, T4/T6–T9, T11, T13–T16** are optional denser coverage, not a stage blocker.

### How to resume next session

1. Read this **Progress** block. Continue **S3** with music **N3** (or composer **G1** in game-template).
2. Build: `scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)`
3. Leave each phase green + committed on its own (plan convention).
4. Re-run `preset_level_test` in game-template after audible DSP changes.

## Context

An external review (`game-template/talk2/`) checked Symphony against 44 audio talks. We re-verified every claim against HEAD `aa3c663d6f`, using float32 ports, C++ benchmarks and a headless repro. The result is `game-template/talk2/VERIFICATION.md`:
- nearly all findings A–W hold;
- about half of talk2's proposed fixes are wrong, so **use the fix specs here, not talk2's**;
- 21 new bugs were found, including two crash/leak-class lifetime bugs (zombie voices N1, `stop()` race B).

Goal: make Symphony production-ready by fixing every verified bug and adopting the worthwhile talk insights, in independently shippable phases. Every phase also has to leave a solid base for the procedural music system (`proc-music-plan.md`). Music-specific features (transport, note pools, recorded phrases, the composer) live in that plan. This plan owns the shared engine prerequisites they rely on.

User decisions:
- **Remove** GrainCloud `pitch_tracking` and PhaseVocoder `time_stretch`.
- **Symphony FDN as an `AudioEffect`** on the spatial wet buses.
- **Patch Godot core** (`AudioStreamPlayer3D`) for a distance-dependent DRR.
- **Full scope**, phased.
- **Sample-rate policy (D, hybrid allow-list).**
  - SFX graphs run at a fixed **48 kHz**, for deterministic authoring and the level contract.
  - Music streams follow the output device **only when it runs at 44.1k or 48k**; otherwise they run at 48k with Godot resampling.
  - Offline and golden renders always use 48k.
- **Note-voice reset (c, state snapshot).** At construction, each resettable operator captures its fresh `export_state` into the arena. Reset is `import_state(snapshot)`, plus clearing its large buffers, plus a reseed. A generic conformance test proves "reset ≡ fresh" for every operator flagged `resettable`. The SFX compile cache does **not** use reset: it re-runs constructors on main in a recycled arena.
- **ModalBank pitch:** a `pitch_ratio` FLOAT pin plus a `root_hz` param, used by SFX pitch variation (it replaces PitchShifter in 11 presets) and by music notes (`ratio = note_hz / root_hz`).

Policy (memory `symphony-presets-no-compat`): pre-production, no compatibility shims; fix it properly. The preset level contract is enforced by `preset_level_test.gd`. Re-tune presets when output changes.

## Conventions for every phase

- **Build:** `scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)`
- **C++ tests:** `bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'` (**219/219** as of S0 close). New files under `tests/modules/` are auto-globbed. Shared helpers live in `tests/modules/symphony_test_helpers.h`.
- **GDScript tests** (in `game-template/`): `node addons/agent_cli/gd.mjs test test/addons/symphony_audio`. Suites: preset_level, finite_oneshot, preset_render, audio_manager_behavior, reverb_routing, spatial_acoustics_integration, skill_handle.
- **TSan:** `bin/godot.macos.editor.arm64.san` rebuilt in P0 (`use_tsan=yes`). P0 baseline: 1 warning. Re-baseline after P1 before closing S0.
- **Operator template:** `nodes/filters/symphony_one_pole.h`.
  - Descriptor and `register_operator()` in the header, called from `register_types.cpp`.
  - Schema extras in `core/symphony_operator_schema.cpp::annotate_authoring_schema`.
  - Cost is `cost_per_sample*BLOCK + extra_cost_fn`.
  - LOD state migration has a 256-byte stack limit (`core/symphony_prepared_graph_package.cpp:122`).
- **Activity/silence pattern to copy:** `nodes/filters/symphony_sv_filter.h` (`silent_blocks`, `ACTIVITY_THRESHOLD`); ModalBank now flushes at `|y1|+|y2| < 1e-9` and can be skipped (`STATEFUL_TAIL`).
- **Denormal guard:** `core/symphony_denormal_guard.h` — install at top of `mix()` / around `render_offline`.
- **Docs to update with any operator or param change:**
  - `user_guide.md` (Operator Reference §487-594, Silence Propagation, Anti-Aliasing, Changelog);
  - regenerate `operator-schema.json` and sync the 4 skill copies (`skills/`, `.claude/skills/`, `.agents/skills/`, `.kiro/skills/` → `godot-audio/references/`);
  - `MIGRATION.md` status line; `dev-log.md`.
- Each phase ends green on both test suites with docs updated, and is committed on its own.

---

## Phase overview

| Phase | Status | Theme | Fixes | Repo | Depends on |
|---|---|---|---|---|---|
| P0 | **Done** `26b1e645be` | Test infrastructure | helpers, alias/spectral asserts, TSan rebuild | godot | — |
| P1 | **Done** `d739292608` | Voice/package lifetime + RT safety | N1, B, A, M, cross-thread fields, H, V, ModalBank flush | godot | P0 |
| P1b | **Done** | Foundation extras (shared with music) | Float→Audio promotion order, SeqLock fix, G seeds, N10 frame counter, N16 `render_offline` | godot | P1 |
| P2 | **Done** | Quick audible wins | N2, N3 | both | P1 |
| P3 | **Done** `dd19939a3a` | Nonlinear and anti-aliasing DSP | C, D, E, N6, N20, J, L, P, DC blocker, Biquad smoothing | godot | P0 |
| P4 | **Done** `1a84c38e46` | Sampler and rate path | S (rate policy D), N18, N19, mix_rate clamp, Oscillator floor | godot | P0 |
| P5 | **Done** `78ea215b35` | Spectral and granular | F, N4, N5, N14, I/U removal, N7, N17, GrainCloud upgrade, stale comments | godot (+ skills docs) | P0 |
| P6 | **Done** `d6bfa4adfc` | Spatial reverb rework | O, T, N15, W, uniformity, FDN AudioEffect, core DRR patch | both + Godot core | P1, P1b, P3 |
| P7 | **Done** `1c50d03cf0` | SFX-side timing | N11 SymphonyClock, N21, asymmetric RTPC (BeatClock + R move to music N1) | both | P1 |
| P8a | **Done** | Voice policy: music-critical part | N9, VITAL tier, N13 | both | P1 |
| P8b | Pending | Voice policy: the rest | Q tiers/caps, N8, N12 | both | P8a |
| P9 | Pending | Performance and polish | state-in-locals convention, K, compile cache, RTSan CI | godot | P1–P5 |
| P10 | Done | Instrument readiness (shared with music) | resettable-state contract and conformance test, ModalBank `pitch_ratio`, music rate-policy helper | godot | P1b, P3 |

## Unified roadmap (technical phases + procedural music milestones)

| Stage | Work | Gate to the next stage |
|---|---|---|
| **S0 Foundation** (= music **N0**) | P0 ✅ → P1 ✅ → P1b ✅ → TSan re-baseline ✅ | TSan stress clean; N1 leak test green; promotion-order and seed tests green; frame counter exact over 10 min |
| **S1 Build-out** (parallel lanes) | Lane A: music **N1** ✅. Lane B: **P3–P2** ✅. Lane C: **P5** ✅ | N1 exact-timestamp tests ✅; P5 green |
| **S2 Instruments** | music **N2** ✅. **P10** ✅ / **P8a** ✅ | Reset conformance ✅; multi-track phrase + 44.1/48k ✅ |
| **S3 Recordings + composer** ← **you are here** | music **N3** ‖ **G1** | Stretch backend + hybrid score; typed score resources + offline ledgers |
| **S3 Recordings + composer** | Music **N3** (Signalsmith, stereo PCM) ‖ music **G1** (resources, composer). **P7** ✅ (landed early) | Fixed hybrid score passes timing and listening checks; 1,000-seed batch passes |
| **S4 Adaptive integration** | Music **G2** ‖ **P8b**. **P6** ✅ (landed early) | Exploration → tension → combat through one playback; DRR and reverb tests green |
| **S5 Polish + qualification** | **P9** → music **V1** | All suites, sanitizers, 30-minute performance run, listening review |

Rules:
- Within a stage, lanes marked ‖ are independent and can be separate branches or PRs.
- A stage starts only when the previous stage's gate is met, except that Lane B/C work in S1 may slip into S2 as long as P3 lands before N2, because instrument graphs use the waveshaping fixes.
- The music plan's N0 is satisfied by S0 and adds no extra work.

---

## P0: Test infrastructure — **DONE** (`26b1e645be`)

1. ✅ Move the file-local helpers into `tests/modules/symphony_test_helpers.h`, and update the includes in the source test files.
2. ✅ Add helpers:
   - `render_graph(GraphDescription, frames, sr)` → `Vector<float>`;
   - `tone_level_db(buf, sr, hz)` (Goertzel);
   - `alias_ratio_db(buf, sr, f0, harmonics)`: energy off the harmonic grid relative to energy on it.
3. ✅ Rebuild `bin/godot.macos.editor.arm64.san` (`use_tsan=yes`) and record the baseline in `MIGRATION.md` (1 TSan warning at P0 baseline).

## P1: Voice and package lifetime + RT safety (N1, B, A, M, H, V) — **DONE** (`d739292608`)

> Implementation landed. S0 leftovers closed (TSan, torch_fire GDScript, T3). Optional denser lifetime cases T2/T4/T6–T9/T11/T13–T16 remain available later. Spec below is the source of truth for any follow-up.

**Lifetime model: single-owner hand-off.** This replaces the ad-hoc stop paths and the release-grace counters. Invariants:
- **I1.** While a voice is reachable by any mixer, only the *mixing thread* writes `current_package`, `current_graph`, `graph_output_node`, `outgoing_package`, `incoming_package`, `transition_*` and `holds_crossfade_token`.
- **I2.** `stop()` is a pure request on any thread: `active.store(false, release)`. No retire, no unregister. Packages stay parked, so stop then start works. This also fixes the silent restart today.
- **I3.** Retirement while reachable happens only on the mixing thread, and always installs the new package (with the `control_package` release store) **before** retiring the old one.
- **I4.** Main reclaims only once the voice is provably unreachable:
  - **reaper:** `get_reference_count() == 1`, i.e. only the manager's `Ref` is left (AudioServer has run its SafeList deleter);
  - **destructor:** refcount 0.
- **I5.** Packages are freed only on main. `GraphPackageRetirement::drain()` returns early off main.
- **I6.** The manager's mix callback reads only per-voice atomics, never package memory.

This was chosen over a per-voice `in_mix` handshake, which needs a second reclaim protocol for N1, retry or deferral at every finalize site, and breaks wrapper restarts. `get_mix_count()` gating was also rejected: it is a racy plain field, freezes when the driver sleeps, and doesn't cover `render_offline` or wrappers.

Steps, in order:
1. **Baseline.** Rebuild with `use_tsan=yes` and record the current Symphony-frame reports.
2. **H denormals.**
   - New `core/symphony_denormal_guard.h`, an RAII guard:
     - x86/x64 MXCSR `|0x8040`;
     - arm64 FPCR bit 24;
     - armv7 FPSCR bit 24;
     - MSVC ARM64 `_controlfp_s(_DN_FLUSH)`;
     - no-op on WASM.
   - The guard skips the write when the flag is already set, and exposes `is_supported()`/`is_flush_active()`.
   - Add `symphony_flush_denormal(float&)`.
   - Install it as the first statement of `AudioStreamPlaybackSymphony::mix()` and around the whole `render_offline` loop (`stream/symphony_authoring.cpp:186`).
   - **ModalBank per-block energy flush.** After the block, `|y1|+|y2| < 1e-9` sets the mode state to 0. With no live modes and zero excitation, `activity = 0`. Add the compiler exception so ModalBank can be skipped, like FeedbackPath at `symphony_graph_compiler.cpp:840`.
   - Audit Biquad, SVF, OnePole, DCBlocker and FDN for the same per-block flush; WASM needs it.
3. **Cross-thread fields → atomics or a single owner** (`stream/audio_stream_playback_symphony.h`):
   - delete `stop_pending`, `last_frame_count`, `pending_is_lod` (the latter becomes an immutable `PreparedGraphPackage::adopt_mode {Swap, Lod, Restart}`);
   - stats become relaxed `std::atomic<float>` fields `stat_mix_us_last`, `stat_rms`, `stat_cost_units`, `stat_cost_pct_ewma`, plus `last_mix_seq`;
   - `current_lod_tier` becomes `atomic installed_lod_tier`, plus a main-only `main_requested_lod` that stops `_sync_voice_lod` from recompiling every frame;
   - `cached_*` and `mix_rate_cached` become relaxed atomics;
   - transition fields stay plain and audio-owned, with an ownership comment block in the header;
   - manager thresholds and `max_voices` become atomics.
   - **M** (`runtime/rtpc_engine.*`): atomic `global_param_count`/`analysis_output_count`. Fill the slot, then `store(idx+1, release)`, with an acquire load in `smooth_all`. `block_alpha` becomes audio-owned, and an `alphas_dirty` atomic triggers the recompute.
4. **B playback refactor** (`stream/audio_stream_playback_symphony.cpp`):
   - Delete `release_pending/grace/ready`, `tick_release_grace`, `_arm_release_grace`, `_finalize_stop` and `abandon_voice_for_destructor`.
   - `start()`:
     - on main, recache metadata, then (on restart) `_publish_restart_package()`, which compiles and sets `adopt_mode = Restart` before the pending exchange; then register;
     - on any thread, set `auto_fire_pending`, `resample_reset_pending` and `position`, and store `active` last.
   - `begin_resample()` moves into `mix()` (it ran DSP on main, which was a race).
   - `_mix_internal` adopts a Restart package by aborting transitions, installing, then retiring the old package.
   - Fix FallbackFadeOut completion so it installs before retiring (`:436-450`).
   - Auto-triggers are fired on audio via a new consumer-side `SymphonyTriggerInput::fire_local()` (`nodes/io/symphony_trigger_input.h`), injected after the trigger clear.
   - Add `_reclaim_unreachable()` (main), used by the reaper and the destructor.
   - Add a DEV-only `PreparedGraphPackage::retired` flag with `DEV_ASSERT(!retired)` in `retire()`, and a DEV `mix_depth` counter asserted to be 0 in reclaim.
5. **N1 reaper + A `try_lock`** (`core/symphony_voice_manager.*`):
   - The lock becomes a `BinaryMutex` that only protects erasing from `active_voices`.
   - `register_voice` is lock-free (SafeList insert; `retained_voices` is main-owned) and uses the `registered_with_manager` flag instead of an O(n) scan.
   - `enforce_voice_limits`: `if (!try_lock()) { metric_enforce_skipped++; return; }` with an RAII unlock. Victims get `v->stop()`, an atomic store.
   - `process_deferred_lod` phase 1 (reap):
     1. collect the indices where `get_reference_count() == 1`, with no lock held;
     2. erase those pointers from `active_voices` under the lock, which allocates nothing;
     3. after unlocking, `remove_at_unordered` in descending order, `_reclaim_unreachable()`, then `drain()`.
   - Remove the `Ref hold(v)` at `:181`.
   - Destructor: `AudioServer::lock(); remove_mix_callback(); unlock();` before tearing down, so no callback is in flight.
6. **V LOD hysteresis** (stats come from `mix()`, decisions from `enforce_voice_limits(now_us)`):
   - Per-voice cost % of the block deadline, with a spike clamp `min(s, 3·ewma + 0.5)` and an asymmetric EWMA (τ_up 50 ms, τ_down 250 ms) reseeded on package install.
   - The snapshot only includes voices with `callback_seq − last_mix_seq ≤ 1`.
   - Thresholds: W = 70, C = 90, R = 55 (new `release_hysteresis = 0.15`).
   - **Engage:** demote when L > C, or when L > W for ≥ 30 ms. Target 65. Pick the highest-cost eligible voice (per-voice floor change ≥ 150 ms ago). Up to 4 steps per callback, 16 above C.
   - **Hold** in [R, W].
   - **Release:** one tier on one voice, only when L < R for ≥ 1.5 s, ≥ 300 ms since the last release, voice dwell ≥ 1 s, and projection `L + ewma·(1/0.6 − 1) < 50`.
   - **Never clear floors above C** (the bug at `:458-462`). Steal while L > C, down to W, using the smoothed L.

Tests: new `tests/modules/test_symphony_lifetime.cpp`, plus updates to `test_symphony_playback.cpp:504` (replace the grace expectations) and `test_symphony_stress.cpp:470`.
- **N1:**
  - T1: unit reap, where unref → `process_deferred_lod` → the object is gone and package counters, `reserved_bytes` and pending retirement are back to baseline;
  - T2 `[Audio]`: real `AudioServer::start_playback_stream`/`stop_playback_stream`. Assert `is_playing()` is still true (documents the upstream behaviour), unref, then within 2 s of `update()` polling the voice is reaped;
  - T3: an external `Ref` keeps it parked, and it is reaped after unref.
- **B:**
  - T4: concurrent mixer thread plus a 500-iteration main loop of stop/start/swap/LOD/param/trigger/drain, with ASan and TSan clean and 0 RT violations;
  - T5: stop then start gives RMS > 0;
  - T6: crossfade completion racing stop and reap never trips the `retired` assert;
  - T7: the editor/unregistered path frees on the destructor;
  - T8: duration-limit self-stop, then reap;
  - T9: an off-main restart has 0 RT violations and the auto-trigger fires once.
- **A:**
  - T10: with the mutex held by a test hook, enforce returns in < 1 ms and the skip metric increments;
  - T11: 256 voices give 0 RT violations.
- **V** (injected clock plus `_test_set_cost_ewma`):
  - T12: 75% for 20 ms → no change; for 40 ms → demote; 62% → hold; 50% → one release at 1.5 s, the next ≥ 300 ms later; 95% → no floor reset;
  - T13: alternating 68↔72% gives zero releases over 5 s;
  - T14: spike clamp;
  - T15: paused voices are excluded.
- **M:** T16, a TSan run of `smooth_all` against concurrent registration.
- **H:**
  - T17: under the guard, `FLT_MIN*0.5 == 0`;
  - T18: `mix()` restores FPCR/MXCSR;
  - T19: ModalBank (t60 10 s, guard off) never holds subnormal state, reaches exactly 0, and sets `activity == 0`.
- **TSan stress** `[Symphony][Stress][Audio]`, 5 s on the `.san` binary with a threaded dummy driver:
  - random start/stop/LOD/swap/param/getters/`set_max_voices` (to force steals), with `update()` each iteration;
  - at the end, all counters are 0, `reserved_bytes` is at baseline, and there are no TSan reports in Symphony frames.
- **GDScript:** add an `audio_manager_behavior_test.gd` case that plays and stops 50 `torch_fire` voices and asserts `SymphonyVoiceManager` `active_voice_count` returns to 0 within 10 frames.

Risks:
- **Parked packages hold arena memory** while a script keeps a stopped playback. Expose a `packages_parked` metric and document it.
- **Steals now take effect on audio, about 1 frame earlier.** Check the GDScript slot bookkeeping.
- **The reap assumes playbacks are main-thread objects** (no off-thread `Ref` resurrection). Document it.
- **Driver lock.** The manager destructor relies on `AudioServer::lock()`; verify that on Android and Web.

## P1b: Foundation extras shared with music (stage S0) — **DONE**

> Start here. Five items; commit when the P1b suite is green. Completing P1b + P1 leftovers closes **S0 / music N0**.

**Float→Audio promotion order** (found by the music plan; `core/symphony_compiled_graph.h:62-66`). Today every promotion buffer is filled at the top of `execute()`, before any operator runs, so an AUDIO consumer of a FLOAT producer in the same graph sees the **previous block's** value. That breaks "pitch and gate change together" for notes, and adds a block of lag to every game-driven control.
- Fix: the compiler attaches each promotion to its producer's position in the topological order (a `promotions_after[op_index]` range), and `execute()` fills them right after that operator runs. Promotions from graph inputs stay at the top.
- Test: GraphInput → MapRange (FLOAT) → Gain (AUDIO amount), where the value changes on block k and the output reflects it on block k, not k+1. Also a note test: a pitch change and a gate on in the same block produce no first-block stale pitch.

**SeqLock fix** (`spatial/symphony_seqlock.h`). `data_` is copied with a plain `memcpy`, a formal data race hidden by `SYMPHONY_NO_TSAN`. The fences are also placed for the wrong pattern.
- Fix: store the payload as an array of `std::atomic<uint32_t>` words (`static_assert(sizeof(T) % 4 == 0)`, pad as needed). The writer does `seq = s+1 (relaxed)`, then `fence(release)`, then relaxed word stores, then `seq.store(s+2, release)`. The reader does `s1 = seq.load(acquire)`, then relaxed word loads, then `fence(acquire)`, then `s2 = seq.load(relaxed)`.
- Remove `SYMPHONY_NO_TSAN`, and move the file to `core/symphony_seqlock.h` because it is now shared by spatial, effects and music.
- Test: a TSan run with one writer and two reader threads for 2 s shows no torn reads (a checksum field) and no reports.
- Users: the spatial engine's SpatialParams, P6's reverb effect params, and music transport snapshots, which no longer need their own mechanism.

**G Per-voice PRNG seeds** (shared with music: the composer's note seeds and the note-voice reseed use `core/symphony_seed.h`).
- New `core/symphony_seed.h`:
  - `splitmix64`;
  - `symphony_node_seed(uint64 voice_seed, StringName node_id, uint32 authored, bool deterministic)` → `uint32`. It mixes `voice_seed ^ hash(node_id)` with `authored`. When `deterministic`, it returns `splitmix(authored ^ hash(node_id))` only, so authored determinism is still available.
- `create_fn` has no node id, so add a **scoped compile context**: a `thread_local` `SymphonyCompileContext{uint64 voice_seed; StringName node_id;}`, set by `GraphCompiler` around each `create_fn` call (`core/symphony_graph_compiler.cpp:757`), mirroring how `SymphonyRenderSeed` is consumed.
- `voice_seed` source:
  - `AudioStreamSymphony::instantiate_playback` draws from a global atomic counter passed through splitmix.
  - It is stored on the playback and reused for every LOD recompile of that voice, so seeds are stable across LOD swaps.
  - Under `render_offline`, `voice_seed = render seed`. This keeps `preset_render_test.gd`'s byte-identical-repeat check green.
- Consumers:
  - `symphony_noise.h`: add a `seed` and a `deterministic_seed` param, plus `export_state`/`import_state` for the LCG and pink rows.
  - `symphony_grain_cloud.h:576`.
  - `symphony_stochastic_trigger.h:88-98`: delete the `&p_arena` derivation.
  - Read the seed via `(int64_t)` from the Variant, and clamp the descriptor max to 2^24−1. Seeds are serialized as float in `.tres`, so this keeps the range honest and removes the float→uint32 UB.
- Delete PhaseVocoder's unused `seed` param (`symphony_phase_vocoder.h:370`).
- Tests:
  - two playbacks of one Noise graph produce different buffers, and their sum is ≈ +3 dB (±0.5), not +6;
  - the same render seed gives identical buffers;
  - an LOD swap keeps the noise sequence continuous;
  - `preset_level_test.gd` passes at all 3 seeds, and the seed loop now actually varies Noise graphs (this closes the S4 test gap).

**N10 Accurate playback position** (it is also the music transport's timing authority) (`stream/audio_stream_playback_symphony.cpp:517-520`). Replace the truncated `position_us` with an `std::atomic<uint64_t> frames_rendered` at the graph rate, and derive `get_playback_position() = frames_rendered / graph_rate`. Test: after 10 minutes of simulated mixes the position error is < 1 frame.

**N16 `render_offline` rate.** It counts frames at the graph rate, but `mix()` resamples to the device rate. Render through the **un-resampled path**: call `_mix_internal` directly at the graph rate, skipping `AudioStreamPlaybackResampled::mix`, so the WAV header, frame counts and pitch are always correct and device-independent. Update `finite_oneshot_test.gd:78-83`, which compensates for the old bug by dividing by `AudioServer.get_mix_rate()`. Test: render a 440 Hz sine with the device at 44.1k and the graph at 48k; `_measure_hz` gives 440 ±1 and frames = duration·48000. Music offline renders use the same path at 48k.

## P2: Quick audible wins — **DONE**

**N2 Godot 3D attenuation filter double-dip.** ✅ In `audio_manager.gd`, 3D pool sets `attenuation_filter_db = 0.0` and `max_db = 24.0`. gdUnit asserts both (and `ATTENUATION_DISABLED`).

**N3 WavePlayer silent on QOA/ADPCM/8-bit WAVs.** ✅ `SymphonyWavePlayer::decode_to_pcm16` at create: 8-bit widens; IMA ADPCM/QOA via `instantiate_playback()->mix` at 1:1 stream rate (`rate_scale = server/stream`). `ERR_PRINT_ONCE` on failure. SpatialGraphWrapper wraps any pathed WAV. Tests: `test_symphony_p2_wave.cpp` (needs `[Audio]` tag for `set_data`).

## P3: Nonlinear and anti-aliasing DSP — **DONE**

**C Saturator 2× half-band** (`nodes/filters/symphony_saturator.h:37-77`):
- Replace it with a correct HIIR polyphase pair:
  - path0 = allpass cascade with coefficients {0.0798664262, 0.5453236511};
  - path1 = cascade with {0.2838293449, 0.8344118915};
  - upsample: `out0 = path0(x)`, `out1 = path1(x)`;
  - downsample: `0.5*(path0(in[1]) + path1(in[0]))`.
- Reuse the existing `allpass_tick`.
- Fix the HB_A1 typo.
- Remove the "~-70dB" comment unless the test proves it.
- Test: an identity-nonlinearity image at fs−f is ≤ −65 dB for 1–15 kHz; passthrough is 0 ±0.1 dB; tanh at drive 8, 5 kHz has alias_ratio(2×) < alias_ratio(1×) − 15 dB. **Do not rely on a passthrough test alone; a swapped-branch build passes that too.**

**N6 Saturator drive modulation spikes.** Run ADAA on `u = x·drive`, so `prev_F` is in the u-domain (same as the Waveshaper), then apply the output gain. Test: an LFO on drive 2↔6 keeps peak ≤ 1.0 + 1e-3.

**D Saturator ADAA epsilon.** Use eps = 1e-3 with a midpoint fallback `f((x0+x1)/2)` (`:134-137`). Test: a 30 Hz, 0.9 amplitude, drive 10 sine has RMS error vs float64 ADAA < −90 dBFS.

**E Waveshaper ADAA quantization** (`symphony_waveshaper.h:59-91`). Use a three-way evaluation:
- both samples in the same table cell → `table_lookup(f, (x0+x1)/2)` (exact for a linearly interpolated f);
- adjacent cells → length-weighted average of the two sub-interval midpoints;
- otherwise → difference of the quadratic antiderivative `F(x) = F[i] + f[i]·t·dx + 0.5(f[i+1]−f[i])·t²·dx`.

Share the f/F tables across instances per preset: a static, immutable, built-once table on the main thread during register, so they no longer take arena space. Test: error vs exact ADAA < −85 dB at amplitudes 0.001 / 0.01 / 0.05 / 0.3.

**N20 Waveshaper FOLDBACK preset** (`:163-169`). Replace it with a continuous triangle fold (`4|frac(x/4+0.25)−0.5|−1` style, scaled). Test: max |Δf| between adjacent table entries < 0.05.

**J FM "anti-alias" pole** (`symphony_fm_oscillator.h:22-25,42,66,111`). Remove the filter and its comment. Clamp the internal-modulator index to `β ≤ max(0, (0.45·sr − fc)/fm − 3)` (Carson + 2 margin). Document that an external `mod_input` is not clamped. Test: fc 5k, fm 3k, requested β 10 gives out-of-band alias energy < −45 dB.

**L Staircase LPF cutoff.** At `core/symphony_graph_compiler.cpp:352`, use `cutoff = min(18000, 0.4·sr)`. Test the inserted biquad's cutoff at 22050 and 96000.

**P FormantOsc wrap.** At `symphony_formant_osc.h:105-118`, use `θ = 2π·(carrier_phase − 0.5)`, `n = floor(ratio)`, `w = ratio − n`, `out = env·((1−w)cos(nθ) + w·cos((n+1)θ))`. Test: at ratio 7.27 and 8.5 the max sample-to-sample jump is < 3× the median step, and harmonics n and n+1 lose < 1 dB.

**DC blocker.** At `symphony_dc_blocker.h:14`, set `R = exp(−2π·15/sr)` (computed in the constructor). Add `export_state`. Test: −3 dB within 15 ±2 Hz at 44.1k, 48k and 96k.

**Biquad cutoff smoothing** (`symphony_biquad_filter.h:80-93`). The AUDIO cutoff pin bypasses the auto-smoother.
- Smooth cutoff and Q per sample with a one-pole (τ = 5 ms).
- Recompute coefficients every 16 samples, and only when the smoothed value moved more than 0.1%. That also removes the per-block sin/cos when the value is static.
- Keep state in locals (see P9).
- Test: a 200 Hz → 4 kHz sweep at Q 4 differs from per-sample-coefficient ground truth by < −50 dB.
- Re-run `preset_level_test` for the 16 game-driven-cutoff graphs.

## P4: Sampler and rate path — **DONE**

**S Graph rate: policy D (hybrid allow-list).**
- **SFX graphs:** set the `AudioStreamSymphony::mix_rate` default to **48000** (`stream/audio_stream_symphony.h:14`). All 93 shipped presets already set 48000, so this only affects new graphs. Make the setter clamp to [8000, 192000] (`audio_stream_symphony.cpp:445`); the spatial wrapper can pass WAV rates below 22050, so widen the hint to match.
- **Shared helper** `core/symphony_rate_policy.h`: `symphony_select_music_rate(device_rate)` returns `device_rate` if it is 44100 or 48000, otherwise 48000. `symphony_offline_rate()` returns 48000.
  - Music streams (`AudioStreamSymphonyMusic`, music N1) call it once at prepare time.
  - Music offline renders always use `symphony_offline_rate()`.
  - A driver restart that changes the rate requires stop and reload; that is documented, not handled live.
- **Rate-sensitivity audit.** Every operator whose behaviour depends on a sample count must be rate-scaled (DC blocker and FM in P3; FDN damping in P6; the K feedback-loop block delay is documented as a sample count).
  - Add a test that renders each approved instrument graph at 44.1k and 48k: the fundamental is within ±1 cent, RMS within 0.5 dB, and envelope timing within 1 ms.
- **Risks:** 96k/192k devices pay one resampling stage for music, which is intended (it caps CPU). Music tests must cover both 44.1k and 48k.

**N18 + Hermite WavePlayer** (`symphony_wave_player.h:55-61,116-118`):
1. Make interpolation reads loop-aware: an index past `loop_end` wraps to `loop_start + (i − loop_end)` (and reflects for ping-pong).
2. Then switch to 4-point Catmull-Rom (`hermite4`), with the same loop-aware fetch for i−1..i+2.

Test:
- a looped sine whose period divides the loop has a max discontinuity at the loop point < 1e-3;
- a 48k→44.1k 10 kHz tone has its alias spur < −28 dB (it is −22.6 dB today).

Defer mips (talk2 #24) to "Later".

**N19 Spatial wrapper not flat when wide open** (`spatial/spatial_graph_wrapper.cpp:96-112`). When the cutoffs are at or above `0.45·sr`, set the OnePole coefficient to pass-through and the SVF to bypass (a `bypass_above_hz` param on both, or let the wrapper skip setting them). Test: open-wrapper response at 10 kHz and 16 kHz is within 0.1 dB (48k), and within 0.1 dB at 8 kHz for a 22.05k WAV.

**Oscillator frequency floor** (`symphony_oscillator.h:79`). Clamp `dt` to `[0, 0.49]` and handle `dt == 0` as DC hold, with no BLEP evaluation when dt == 0. Test: freq 0 gives constant output.

## P5: Spectral and granular

**PhaseVocoder** (`nodes/spectral/symphony_phase_vocoder.h`):
- **F pitch-shift fix.** Implement the version from the notes:
  - per dest bin, the strongest contributing source wins (magnitude sums are kept);
  - `synth_phase[dest] += ratio·(k_src·adv + delta_k_src)`, once per dest per frame.

  Do **not** use the one-liner from the findings, which double-advances when ratio < 1.
- **N4.** Replace the `wrap_phase` while-loops (`:79-88`) with `x − 2π·floor((x+π)/(2π))`.
- **N5.** **Remove `time_stretch`** (user decision): param, pin, the analysis/synthesis hop split at `:96,111,157`, and docs.
- Delete the stale leak comment (`:29-31,40`). Make `export_state` include the input and output rings, or explicitly exclude PV from LOD migration (declare `state_size = 0`); pick export if it fits within 256 B, otherwise exclude.
- Tests (new, in `test_symphony_spectral.cpp`):
  - 234 Hz shifted −12, −5, −1, +1, +7, +12 st has peak frequency within ±5 cents;
  - a 12-harmonic saw at +7 st has ≥ 90% of its energy within ±10 c of target;
  - N=8192/overlap 8 per-frame cost < 150 µs on the dev box (release build, sanitizer skip as in `README.md:95`).

**SpectralGate:**
- **N14.** Normalize magnitudes by `Σw/2` (`symphony_spectral_gate.h:92,116`) so the threshold is dBFS. Remove the unnormalized note in `test_symphony_spectral.cpp:315` and assert that a −30 dBFS tone passes and a −50 dBFS tone is gated at threshold −40.
- Fix the `int32 input_samples_fed` overflow (`:179`) by making it `uint64` or wrapping it modulo the hop.
- Delete the stale comment at `:24-26,325`.

**GrainCloud** (`nodes/synthesis/symphony_grain_cloud.h`):
- **I/U.** **Remove `pitch_tracking`** (user decision): the pin/param, `detect_pitch`, the capture ring used only for YIN, and the playback_rate scaling at `:404-410`. Update the cost model and `test_symphony_stress.cpp:327-337`.
- **N7.** Density 0 produces no NaN/inf schedule. Clamp the interval computation and, when density changes by more than 2× (or crosses 0), reschedule `schedule_counter = min(counter, new_interval·rand)`.
- **N17.** In live mode, clamp `offset_behind ≥ len·max(0, rate−1) + 1` (`:396`).
- **Upgrade (talk2 #26, all "Yes"):**
  - `MAX_GRAINS` 8 → 32 with an atomic `dropped_grains` counter exposed via the RT metrics; trigger spawns steal the oldest grain instead of dropping;
  - optional overlap normalization `gain /= sqrt(max(1, density·size_s·E[w²]))`, **on by default**;
  - window shapes `hann | tukey | expodec | rexpodec`, baked into the shared table;
  - ranges: size 2–500 ms, density up to 500/s;
  - `pitch_range_st` (±24) and `reverse_prob`;
  - `extra_cost_fn` scales with `min(32, density·size)`.
- Tests:
  - at 50/s × 200 ms, drops = 0 with 32 slots;
  - RMS at 1 vs 8 average overlap is within 1.5 dB with normalization on;
  - a density ramp 0 → 50 produces its first grain within 50 ms;
  - with rate 2 and position 1, no read crosses the write head (assert inside a test build);
  - the level contract still passes.
- Docs:
  - user_guide GrainCloud row and Synthesis section;
  - "granular recipes" (rain, gravel, debris, fire crackle) as 3 new `.tres` templates under `game-template/audio/presets/graphs/` plus a short section in the skills `presets-and-patterns.md`.

Minor: in the Noise `pink_index`, use `uint32_t` and `symphony_ctz((pink_index + 1) | 0x80u)` (`symphony_noise.h:19,41-45`; `symphony_pin_types.h:29`).

## P6: Spatial reverb rework (O, T, N15, W, uniformity; plus 3 newly found routing bugs) ✅

Newly found during design:
- **(a) Portal gain is applied twice to the wet signal.** The graph's `spatial_gain` already includes it, and `audio_manager.gd:370` multiplies it in again.
- **(b) The 0.5 m reverb proxy is moved before the player's apparent-position write**, so the player can drop out of its proxy for a frame. That means no wet signal and a click.
- **(c) The slot `wet_gain` fade (0.25 s) truncates tails.**

1. **Shared DSP cores** (header-only, no Object dependencies), in a new `modules/symphony/dsp/`:
   - **`symphony_fdn_core.h` → `SymphonyFDNCore`:**
     - `design(sr, min_lines, mean_delay_s, rt60, hf_ratio, max_line, Design&)`, pure and allocation-free;
     - **delays:** log-spread over SPREAD 2.3, then bump each target until `gcd = 1` with all earlier lines. No prime table, so it works at any rate. Use 8 lines whenever `N·mean < 0.125 s` (mode density);
     - per-line `g_i = 10^(−3 d_i/(rt60·sr))` and an HF shelf `r_i`, `a_i = (1−r_i)/(1+r_i)`, exact at DC and Nyquist;
     - energy-normalized `out_gain = 0.905·√((1−e^{−x})/(2N))`;
     - **sign vectors (verified, not Hadamard rows):**
       - `b = [1,1,1,−1,1,−1,−1,−1]`
       - `c_L = [1,1,1,1,1,1,−1,−1]`
       - `c_R = [1,1,1,−1,1,−1,1,1]` (`c_L·c_R = 0`, IACC about 0.3)
       - N=4 node: `b4 = [1,1,1,−1]`, `c = 1`;
     - fractional taps gliding with a 50 ms one-pole, the existing fast WHT scaled by `1/√N`, `undenormalize()`, and per-block ramps for g, a and out_gain.
   - **`symphony_shoebox_er.h`:** move `compute_shoebox_reflections()` here unchanged, and add `compute_relative(dims, reflection, c, TapSet&)`:
     - default off-centre listener and source (all 6 taps are distinct, even for cubes);
     - delays relative to the direct path, with per-wall L/R pan gains;
     - `reflection = √(1−α_mean)`;
     - an A/B tap-set crossfade of about 1024 samples on change.
2. **Port the graph nodes onto the cores.**
   - `nodes/delay/symphony_fdn_reverb.h` keeps its pin and param names. `room_size` maps to the mean delay, `damping` to `hf_ratio = 1−0.9·damping`, and `num_lines` becomes the minimum line count.
   - Fix the pre-delay: always write the buffer, and smooth the fractional read.
   - `nodes/delay/symphony_early_reflections.h` uses the off-centre positions, fractional taps and crossfade.
   - **The 4 FDN presets** (reverb_room, footstep_stone, thunder, cave_drip) **will be about 6 dB quieter**: re-tune their Mix nodes by ear and against the level contract.
3. **`AudioEffectSymphonyReverb`** (new `modules/symphony/effects/audio_effect_symphony_reverb.{h,cpp}`; add `effects/*.cpp` to SCsub; `GDREGISTER_CLASS` in `register_types.cpp`):
   - Properties:
     - `rt60`, `hf_rt60_ratio`, `room_dimensions` (Vector3);
     - `er_level`, `er_reflection`;
     - `auto_predelay`/`predelay_ms`, `wet`, `dry`;
     - `set_room(rt60, hf, dims, refl)` batches them;
     - static `compute_first_reflection_ms(dims)`.
   - Main-thread setters publish a `Params` POD through the P1b-fixed SeqLock (`core/symphony_seqlock.h`). The instance calls `try_load` at the start of each `process()`, then `design()`/`compute_relative()` at its own sample rate.
   - Chain: `in = L+R` (calibrated for uniformity = 1 sends) → input line → ER taps (stereo) + late FDN after pre-delay (= first reflection) → `dry·src + wet·(er_level·ER + late)`.
   - `process_silence() = false`, so tails ring out and idle buses cost nothing.
   - About 300 KB per instance at 48k, allocated in `instantiate()` on main.
   - Add `doc_classes/AudioEffectSymphonyReverb.xml` and list it in `config.py`.
4. **Godot core patch for DRR** (`scene/3d/audio_stream_player_3d.{h,cpp}`, `doc/classes/AudioStreamPlayer3D.xml`):
   - New property **`direct_volume_db`** (default 0 behaves identically to upstream). It applies to the direct path but not to the Area3D reverb send.
   - Split the switch into `_get_model_attenuation_db`; `_get_attenuation_db = model + direct_volume_db + volume_db`; `_get_send_attenuation_db = model + volume_db`.
   - In `_update_panning`, copy the panned pre-gain vector into `listener_send_vector` scaled by the send attenuation, before multiplying the direct vector. Pass it to `_calc_reverb_vol`, renaming `direct_path_vol` to `send_path_vol`.
   - The attenuation-filter semantics stay as upstream (the game sets it to 0).
   - Bind it with a NaN guard. Add a core test `tests/scene/test_audio_stream_player_3d.cpp` (default, round-trip, NaN).
5. **Spatial engine/pool API:**
   - **`spatial/reverb_pool.*`:** the new `SlotParams` holds decay_time, absorption_high, hf_ratio, volume, dims, er_reflection and critical_distance.
     - Dims are the mean of authored dims; otherwise aspect `(1.25,0.64,1.25)·∛V`.
     - `hf_ratio = clamp(α_mean/α_high, 0.1, 1)`, with `α_mean = 0.161V/(S·T60)`.
     - Remove `room_size`, `pre_delay_ms` and the room-size mappings.
     - `assign()` takes the authored dims (`spatial_acoustics_engine.cpp:282-298`).
   - **New pure `spatial/reverb_level.h`:**
     ```
     r_c  = clamp(0.057·√(V/T)·10^(bias/20), 0.05, 50)          // per emitter, per frame, main thread
     w    = send · min((r_ref/r_c)·exp(−0.02·r/T), a_r·10^(−min_drr/20))
     amount = clamp(w, 0, max_amount)
     ```
     - Use the emitter's own V/T60, falling back to the slot's.
     - `a_r` comes from `SymphonyVoicePool::get_slot_attenuation_volume`.
     - Portal gain is not included (fixes (a)).
     - ProjectSettings `audio/symphony/reverb_{reference_distance=1, min_drr_db=-12, max_amount=2, drr_bias_db=0}`.
     - The existing clamps at `reverb_pool.cpp:101`, `spatial_acoustics_engine.cpp:900` and `room_estimator.cpp:78` stay (send keeps meaning "enclosure").
   - **Bindings:**
     - add `get_reverb_slot_{hf_ratio,dimensions,volume,er_reflection,critical_distance}` and `get_emitter_{critical_distance,reverb_amount}`;
     - remove `get_reverb_slot_room_size`;
     - replace `get_reverb_slot_damping` with `get_reverb_slot_absorption_high`.
6. **GDScript** (`addons/symphony_audio/audio_manager.gd`):
   - 3D players get `attenuation_filter_db = 0` (already done in P2) and `direct_volume_db` reset on play and release.
   - Proxy: `radius = 2.0`, `reverb_bus_uniformity = 1.0`; move the proxy position update after the apparent-position write (fixes (b)).
   - Wet buses use `AudioEffectSymphonyReverb` (`dry 0`, `wet 1`), and the `wet_gain` push is removed (fixes (c)).
   - Each frame, call `fx.set_room(decay, hf_ratio, dims, er_reflection)` and set `area.reverb_bus_amount = SAE.get_emitter_reverb_amount(slot)`, deleting the portal multiply.
   - Split the volume stack for 3D players: `volume_db = initial + rtpc + fade` and `direct_volume_db = attenuation`.
   - Delete `_resolve_slot_shoebox_dimensions`, `_early_reflection_predelay_msec`, the `ER_*` constants and the stale header comment.
7. **Tests:**
   - **C++** `test_symphony_fdn_core.cpp`:
     - delays coprime and increasing, with Σ ≥ 0.125·sr at 44.1/48/96/192k;
     - b, c_L and c_R are not H8 rows, and `c_L⊥c_R`;
     - shelf gain is exact at DC and Nyquist;
     - IR T30 within ±5% of RT60 for {0.4,1,2.5} s, and matching between 48k and 96k within 3%;
     - HF/LF decay ratio ≈ hf_ratio ±15%;
     - L+R energy 0 ±1 dB, |IACC| < 0.4;
     - parameter changes produce no steps and no NaN;
     - the tail decays below 1e-6.
   - **C++** `test_symphony_early_reflections.cpp` gains: distinct taps for a cube; first reflection for 10×5×8 = 5.4 ±0.3 ms; monotonic with room size; crossfade continuity.
   - **C++** `test_symphony_reverb_effect.cpp`: wet-only at dry 0; uniform-input energy ≈ 1; auto pre-delay = first reflection; a second thread calling `set_room` during `process()` causes no crash or NaN.
   - **C++** `test_symphony_spatial_reverb.cpp`: rewrite the slot-param test and the `sizeof` check at `:121`.
   - **C++** `reverb_level` cases: r_c(400 m³, 1.2 s) = 1.04 m; w ∝ 1/r_c; the floor engages near max distance.
   - **GDScript** `reverb_routing_test.gd`:
     - change the type to `AudioEffectSymphonyReverb`;
     - the expected amount comes from the engine;
     - **new:** in the same room, the wet amount at 1 m vs 6 m has ratio ≥ 0.8 while `direct_volume_db` drops > 3 dB;
     - the volume-stack split is correct;
     - `uniformity == 1` and `attenuation_filter_db == 0`.
   - **GDScript** `spatial_acoustics_integration_test.gd:387-449`: predelay ≈ **5.4 ± 0.5 ms** (it was 22.3, the mean-wall heuristic), aspect-ratio dims, and portal gain applied once.
8. **Docs:** user_guide, MIGRATION (removed APIs, new settings), `spatial/HANDOFF_NOTES.md`, and the game-template skills `api-reference`/`spatial-acoustics`/`dsp-graphs` (all 4 copies).

Risks:
- **Memory** is about 12 MB, at roughly 300 KB × dry buses × 8 slots. Add a setting listing which dry buses get wet buses (skip Music and UI), and use 4 slots on mobile.
- **Surround modes** duplicate the FDN per channel pair. Process pair 0 only, or rotate the c vectors.
- The DRR floor and r_ref are taste parameters, so they are exposed as settings.

## P7: SFX-side timing (stage S3) ✅

> **Handed to music N1:** the **R** (sample-accurate triggers) and **N BeatClock** specs below are implemented as part of music milestone N1. The procedural transport becomes the timing authority, and BeatClock observes it in PROCEDURAL mode. The legacy-mode fixes below (loop wrap, crossed beats, `AudioStreamInteractive` position) are still part of N1's exit gate. They stay in this file as the spec. P7 itself only delivers N11, N21 and asymmetric RTPC smoothing.

**R Sample-accurate triggers** *(✅ done in music N1; reuses the P1b `frames_rendered` counter)*:
- Add `uint64_t target_frame` to the TriggerInput `QueueEntry` (`nodes/io/symphony_trigger_input.h:39-49`).
- New API `AudioStreamPlaybackSymphony::trigger_at(name, frame)` and `get_frames_rendered()`.
- In `execute()`, an entry fires at `offset = clamp(target_frame − block_start_frame, 0, BLOCK−1)` when it falls in the block, stays queued if it is in the future, and fires at offset 0 if it is late.
- `trigger(name)` stays "ASAP" (target 0).
- GDScript: expose it through `SymphonyVoicePool`/`AudioManager` as `trigger_at(handle, name, time_s)`.
- Test: a trigger at frame 1000 starts an ADSR at exactly sample 1000 ±0.

**N11 SymphonyClock** (`nodes/timing/symphony_clock.h:33,43-61`):
- fire the first tick at counter 0 (downbeat at t = 0);
- add a FLOAT `bpm` input pin (smoothed per block, keeping phase continuous), a `reset` trigger input and a `phase` output;
- new tests (none exist today): ticks land at exact frames for 120 BPM at 48k, the first tick is at frame 0, and a bpm change mid-beat preserves phase.

**N BeatClock** (`runtime/beat_clock.cpp/.h`) *(✅ done in music N1)*:
- Replace `beat = t·bpm/60` with an incremental accumulator: `beat_acc += Δpos·bpm/60` per process.
  - If Δpos < 0 (loop wrap or stream switch): when `wrapped_to ≈ 0`, treat it as a loop and continue the accumulator from the loop length. Otherwise re-base: `beat_acc = new_pos·bpm/60`, emitting a new downbeat.
  - On `set_bpm`/`set_beats_per_bar`, keep `beat_acc` and change only the slope.
- Emit **every** crossed beat and bar, capped at 8 per frame.
- Make `get_current_beat()`/fraction use the same smoothed time as the signals (`:97-100` vs `:159`).
- Add `fire_ahead_s` (default 0): beats fire that much *early* with their exact target time in the signal args (`beat_hit(index, target_time)`), so audio can be scheduled with `trigger_at`. Visual consumers keep `fire_ahead_s = 0` and use the heard time.
- **AudioStreamInteractive:** `get_playback_position()` returns 0 upstream (`modules/interactive_music/audio_stream_interactive.cpp:1040-1042`).
  - Fix in this fork: track the current clip's playback position in `AudioStreamPlaybackInteractive` (it mixes per-clip playbacks; return that clip's `get_playback_position()`).
  - Alternatively, MusicSystem INTERACTIVE mode drives BeatClock from accumulated `delta` at known bpm.
  - **Recommend the core fix** (small, contained in this fork).
- Tests (new `test_symphony_beat_clock.cpp`):
  - a looped stream emits beat 0 and bar 0 on every loop;
  - a 400 ms hitch at 180 BPM emits 2 beats in order;
  - set_bpm mid-bar is monotonic with no lost or duplicate index;
  - interactive mode advances.
- GDScript: in `music_system.gd:119-136,796-811`, handle the new signal args and the wrap events. Run the existing music tests.

**N21 MusicStateGraph validation** (`runtime/music_state_graph.cpp:60-64,116-146`). `validate()` errors on a missing or ≤ 0 bpm and on a missing beats_per_bar; remove the silent 120/4 fallback in `music_system.gd:470-471,780-781`.

**Asymmetric RTPC smoothing** (`runtime/rtpc_engine.cpp:80-97`). Add `attack_ms`/`release_ms` per parameter (the release alpha is used when the value decreases). **M** is done in P1.

## P8: Voice-policy unification (P8a in stage S2, P8b in stage S4)

**P8a (music-critical; lands before music G2):** items 1 (the `tier` field and VITAL semantics only), 2 (N13) and 5 (N9).
- The procedural music playback registers as **VITAL** and reports its **aggregate** cost to the CPU manager, which may LOD-demote it but never steals it.
- Its internal note voices are not manager voices. The music plan's note pool owns them.

**P8b:** items 1 (category caps), 3 (N8), 4 (N12) and 6 (the GDScript `_admit_from` tiers and caps), plus the remaining tests.

Today there are three inconsistent policies:
- GDScript `_admit_from` (importance);
- the C++ dispatcher (raw priority, then steal mode);
- the CPU manager (stream `voice_priority`, then pre-attenuation RMS).

Target: **one importance key computed in `SymphonyVoicePool`** and used by all three.

1. **Tiers + caps** (`runtime/sound_event.h`): add `tier` (VITAL/IMPORTANT/OPTIONAL) and `max_voices_per_category` in project settings (`audio/symphony/category_caps/*`).
   - importance = tier band (VITAL never stolen by a lower tier) + priority × distance factor × weight × category weight;
   - make the loudness term "Later": it needs post-attenuation RMS for file streams.
2. **N13.** In `voice_manager.cpp:134-152,540`, reset `position` in `_activate_slot`, and use a distance factor of 1 for non-positional voices.
3. **N8.** Apply the per-event `max_voices`/`steal_mode` and the category cap on the `victim_slot` reclaim path (`event_dispatcher.cpp:125-170`), sharing one helper with `:203-238`.
4. **N12.** STEAL_QUIETEST compares like with like: use `rms_valid ? rms : NaN`, prefer the voices that have RMS, and fall back to the lowest importance (`event_dispatcher.cpp:70`).
5. **N9.** The CPU manager (`core/symphony_voice_manager.cpp:476-529`) uses the pool's importance:
   - `SymphonyVoicePool` pushes `set_voice_importance(float)` to the playback each frame;
   - the manager ranks by that, with ties broken by RMS;
   - VITAL is never budget-stolen (it can be LOD-demoted);
   - remove `AudioStreamSymphony.voice_priority`.
6. GDScript `_admit_from` (`audio_manager.gd:1131-1152`) uses `get_slot_importance` and respects tiers and caps (reject when a cap is hit and no same-category victim of lower importance exists).

Tests:
- C++ `test_symphony_voice.cpp`: a cap of 2 on a category rejects or steals within the category; VITAL survives at 100% budget; the reclaim path honours `max_voices`; STEAL_QUIETEST in a mixed pool picks the true quietest.
- gd `audio_manager_behavior_test.gd`: tier and cap admission.

## P9: Performance and polish

**State-in-locals convention.** Measured 2–2.5× speedup, bit-exact. In every per-sample loop, load member state into locals before the loop and store it back once. Apply to:
- biquad, SV filter, oscillator, one-pole, envelope_float, compressor, LFO, ADSR, FM oscillator, clock, DC blocker, delay line;
- representative paths: `nodes/filters/symphony_biquad_filter.h:85-93`, `nodes/filters/symphony_sv_filter.h:81-101`, `nodes/generators/symphony_oscillator.h:74-138`.

Keep the `switch` as is (≤ 10% gain). Hot loops must be bit-exact before and after: add a test that renders each touched node with fixed inputs and compares against a pre-change capture committed as a test fixture. **Do not** apply talk2's ModalBank restrict/partial-sum rewrite (measured 2× slower). Document the convention in `user_guide.md` "Writing operators".

**K Feedback-loop delay.**
- Make `FeedbackPath` zero-extra. The back-edge's persistent output buffer already supplies one block of delay, so FeedbackPath becomes a pass-through (copy input → output, no `prev_block`). The compiler schedules it last in topological order, so readers earlier in the order see the previous block's value and the loop is exactly 1 block.
- Fix the docs (`symphony_feedback_path.h:11`, `user_guide.md:553`): a bare edge or FeedbackPath loop is 1 block (64 native / 32 web), and short loops belong inside one operator.
- Add a graph-level test: an impulse through Mix → FeedbackPath returns after exactly 64 samples.

**Compile per play.** (This does not use the P10 reset contract: SFX plays happen on main, so constructors simply re-run inside a recycled arena; only music note voices reset on the audio thread.) Cache the flattened, validated, topologically sorted plan per `AudioStreamSymphony` (invalidated on resource change), and keep a small per-stream free-list of retired arenas of the same size class, which `drain()` returns to instead of freeing. Test: the second `instantiate_playback` skips flatten/sort (counter) and allocates 0 new arenas when one is free.

**RTSan CI** (Later, document only). A Linux job with upstream LLVM ≥ 20 and `-fsanitize=realtime`, annotating `mix()`/`render_offline` with `__rtsan_realtime_enter/exit` declared `extern "C"`, plus a dummy-driver stress scene, because unit tests bypass the mix callback. Record it in `MIGRATION.md` as follow-up.

## P10: Instrument readiness (stage S2, before music N2)

**Resettable-state contract (decision c).**
- `SymphonyOperator` gains a `bool resettable` descriptor flag and a non-virtual `reset_for_note(uint64_t seed)`:
  1. `import_state(pristine_blob)`;
  2. `clear_large_buffers()` (a virtual, default no-op; delay lines and capture rings zero only their *used* region, tracked by a high-water mark);
  3. `reseed(symphony_node_seed(seed, node_id))` (a virtual, default no-op).
- At construction, on main, the compiler calls `export_state` once and stores the **pristine blob** in the arena next to the operator (arena sizing adds `state_size`).
- Lift the 256-byte stack limit in `migrate_compatible_state` (`core/symphony_prepared_graph_package.cpp:122`) by using an arena scratch area sized to the largest `state_size` in the graph.
- Fill the `export_state` gaps found in verification: Noise (LCG + pink rows), DC blocker, PhaseVocoder rings (or mark it non-resettable), and ModalBank `import_state`, which must recompute coefficients for imported modes (`symphony_modal_bank.h:138-140`).
- **Generic conformance test** `test_symphony_operator_reset.cpp`. For **every registered operator** flagged `resettable`, build a minimal graph with default params, drive deterministic inputs, and render N blocks. Then `reset_for_note(s)` and render again. The output must be bit-identical to a freshly constructed operator with seed s. Operators not flagged must appear on an explicit allow-list in the test, so a new operator can't silently skip the check.
- The music note-pool validator (N2) rejects graphs that contain non-resettable operators.
- Initially resettable: oscillators, FM, noise, ADSR/envelopes, filters (biquad, SVF, one-pole, DC), math/utility, waveshaper, saturator, ModalBank, WavePlayer (resident), MusicNoteInput.

**ModalBank `pitch_ratio` + `root_hz` (decision).**
- Keep `base_freq[]`, `t60[]` and `gain[]` in the arena, not only the coefficients.
- New FLOAT input `pitch_ratio` (0.125–8, default 1) and a param `root_hz` (the authored fundamental, used by music to compute `ratio = note_hz / root_hz`).
- Recompute coefficients when the ratio changes by more than 0.05%: per block while gliding, and immediately at a note boundary via `reset_for_note`. Cost is about 64 × (sin, cos, exp) ≈ a few µs.
- Modes above `0.45·sr` are muted (gain 0, state cleared). The per-block energy flush from P1 lets voices report release-complete.
- Migrate the 11 PitchShifter+ModalBank presets (bell_chime, bouncing, chain_rattle, crystal_resonance, crumbling, mechanical_gear, rope_chain, rolling, shield_impact, sword_clash, stone_impact) **where the PitchShifter only provides pitch variation**: drive `pitch_ratio` from the existing pitch MapRange and delete the PitchShifter. Re-tune them to the level contract.
- Tests:
  - ratio 2 doubles every mode frequency (Goertzel, within ±1 c);
  - a mode above 0.45·sr is silent;
  - a glide produces no click (max |Δ| bound);
  - a migrated preset renders without the 25–50 Hz amplitude modulation (alias_ratio and the modulation spectrum).

**Music rate-policy helper.** See P4 S (`core/symphony_rate_policy.h`). P10 adds the 44.1k/48k instrument-graph equivalence test once MusicNoteInput exists in N2.

## Verification (each phase and at the end)

1. **Build** the editor with tests, and also the `use_tsan=yes` build for P1 and P6:
   `scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no`
2. **C++:** `bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*,*AudioStreamPlayer3D*'`.
   - Every new test listed per phase must pass, and the total goes up from the 182 baseline.
   - The P1 lifetime and stress cases must also pass on the `.san` binary with no reports in Symphony frames.
3. **GDScript** (in `game-template/`): `node addons/agent_cli/gd.mjs test test/addons/symphony_audio`. Every suite stays green; the level contract is re-tuned where output intentionally changed (P3 Biquad, P5 GrainCloud, P6 FDN presets, P10 ModalBank migration).
4. **Leak check (N1):** `audio_manager_behavior_test.gd` `test_fifty_torch_fire_play_stop_leaves_no_manager_voices` — `active_voice_count == 0` and `packages_active == 0` after the batch.
5. **Listening pass** (manual, in a test scene). Check each of these:
   - stopping and fading voices no longer produces a low-pass sweep (N2);
   - layered noise ambiences sound wider and not phasey (G);
   - walking away from a source in a hall keeps the wet level steady while the dry falls (T);
   - a closet and a hall at the same distance sound clearly different (N15);
   - room or portal changes don't click;
   - quiet tails through a Waveshaper are clean (E);
   - a looped music stream fires the bar-0 signal on every loop (N).
6. **Docs:**
   - regenerate `operator-schema.json` from `AudioStreamSymphony.get_operator_schema()` and sync all 4 skill copies;
   - add a user_guide changelog entry per phase in the existing format (bold operator name, then the bug with numbers, then the fix);
   - update the `MIGRATION.md` status line with the new test count and TSan result;
   - in `game-template/talk2/VERIFICATION.md`, mark each item fixed with its commit.

## Explicitly NOT doing (from VERIFICATION §3)

- RCU voice snapshot
- Elliptic BLEP
- talk2's tempo map
- ray-traced IR / convolution
- grouped FDN / matrix rotation
- ModalBank restrict rewrite
- SPSC RT log ring (the DEV print traps anyway)
- WavePlayer mips
- hard sync
- wavetable node
- PV phase locking
- PSOLA PitchShifter
- ADPF

Revisit these on demand.
