# Spatial Acoustics Handoff Notes — Next Session

**Date:** 2026-08-28 (updated after Task 12 — S5 complete)  
**Branch:** `features/symphony_fixed`  
**Plan source of truth:** `modules/symphony/Spatial_Acoustics_for_Symphony_Plan.md`  
**Reference addon:** `/Users/luong.pham/Work/spatial_audio_player_3d`  
**SeqLock notes:** `/Users/luong.pham/Work/game-template/docs/audio/plan/notes_future_milestone.md`

---

## Where We Are

| Task | Scope | Status |
|------|-------|--------|
| **Task 1** | 3D playback path — play_event_3d, AudioStreamPlayer3D pool, SoundEventPlayer3D | ✅ Done |
| **Task 2** | Attenuation curves — Natural, Log Reverse, Inverse Square, inner_radius, falloff_distance | ✅ Done (in Task 1) |
| **Task 3** | AcousticMaterial resource — 9 fields, 12 presets as .tres | ✅ Done |
| **Task 4** | AcousticBody3D — HashMap<ObjectID, AcousticMaterial*> registry, O(1) lookup | ✅ Done |
| **Task 5** | SpatialParams + engine skeleton — SeqLock, IIR smoothing (α=0.75), snap-on-first | ✅ Done |
| **Task 6** | Occlusion solver — alternating rays, sqrt correction, material transmission | ✅ Done |
| **Task 7** | In-graph DSP — spatial graph wrapper, air absorption, ParameterSmoother log mode | ✅ Done |
| **Task 8** | Probe scheduler and cache — budgeted rays, distance priority, spatial-hash cache | ✅ Done |
| **Task 9** | Room estimation + physical RT60 — Fibonacci fan, Sabine/Eyring | ✅ Done |
| **Task 10** | Shared reverb pool — RT60-proximity clustering, N slots, migration crossfade, anti-regression | ✅ Done |
| **Task 11** | Propagation delay — SoundEvent speed_of_sound/enable, deferred voice start (no SceneTreeTimer) | ✅ Done |
| **Task 12** | Volumetric occlusion — sphere-volume sampling, centre-validation, graduated occlusion; distance→air_cutoff wiring | ✅ Done |

**Phase S5 (Spatial Acoustics Core) — COMPLETE & TSan-validated.** Next: S6 (Portal Propagation & Reflections, Task 13+).

**Tests:** 90/90 pass (195,221 assertions), incl. 12 [Symphony][Spatial][Reverb] + 12 [Symphony][Spatial][Propagation] + 11 [Symphony][Spatial][Volumetric] cases.
**TSan:** 90/90 pass under ThreadSanitizer, 0 data races (S5 gate cleared).
**Last build:** ~11.5s editor arm64, clean (2 pre-existing unrelated warnings).

---

## ⚠️ Deferred Follow-Ups (COME BACK TO THESE)

Flagged with the user 2026-08-28; explicitly deferred, not yet actioned.

1. **Propagation-delay countdown uses wall-clock, not a real delta (Task 11).**
   `SymphonyVoicePool::process_frame()` decrements `pending_start_delay_s` using
   `OS::get_ticks_usec()` between calls (no delta param, since it's bound arg-less to
   GDScript). Consequence: the countdown drifts on frame hitches and keeps running while
   the game is *paused* (counts real time, not game time). Fine for ~0.1–1 s one-shot
   arrivals; revisit if long delays or pause-correctness matter. Fix option: add an
   explicit `delta` param (or drive `tick_deferred_starts(delta)` from `_physics_process`).

2. **Volumetric occlusion has no real-physics automated test (Task 12).**
   Driving `PhysicsServer3D` directly in the headless doctest harness SIGSEGVs, so the
   graduated-occlusion math is tested via the injectable LOS predicate
   (`OcclusionSolver::compute_volumetric<ClearLosFn>`). The physics wrapper
   `solve_volumetric()` is a thin pass-through but is **not** exercised by an actual
   raycast test. TODO: add a GdUnit4 integration test in `game-template` (runs inside a
   full SceneTree) that places a real collider between a sized source and the listener
   and asserts graduated occlusion.

4. **Reverb-pool R4 ("no AudioServer bus churn") is only guaranteed C++-side.**
   `ReverbPool::touched_audio_server()` proves the C++ manager never mutates AudioServer.
   The actual bus creation is GDScript (plan option b: create N reverb send buses once at
   init). The real R4 guarantee therefore depends on that GDScript honoring "create buses
   once, never at runtime." TODO: when the GDScript lands, add a GdUnit4 assertion that
   the AudioServer bus count is constant across room transitions.

---

## Files Created (modules/symphony/spatial/)

| File | Purpose |
|------|---------|
| `acoustic_material.h/.cpp` | AcousticMaterial Resource (absorption/scattering/transmission per band) |
| `acoustic_body_3d.h/.cpp` | Node3D with static collider→material registry |
| `symphony_seqlock.h` | Lock-free SeqLock template with TSan suppression |
| `spatial_acoustics_engine.h/.cpp` | Singleton: emitters, IIR smoothing, occlusion+room integration, scheduler+cache, graph wrapper access |
| `occlusion_solver.h/.cpp` | Static solve() with alternating rays + sqrt correction |
| `spatial_graph_wrapper.h/.cpp` | Factory building spatial AudioStreamSymphony for plain WAVs |
| `probe_scheduler.h/.cpp` | Budgeted ray scheduler (distance priority, round-robin, 10Hz base) |
| `probe_cache.h/.cpp` | Spatial-hash cache for room probe results (time invalidation, LRU) |
| `room_estimator.h/.cpp` | Fibonacci ray fan, Sabine/Eyring RT60, volume/surface/openness |
| `reverb_pool.h/.cpp` | Shared reverb pool: RT60-proximity clustering, N pre-alloc slots, per-emitter send + migration crossfade, contiguous GPU param block, `touched_audio_server()` anti-regression (never mutates AudioServer) |
| `presets/*.tres` | 12 acoustic material presets |

### C++ Modified
| File | Change |
|------|--------|
| `runtime/sound_event.h/.cpp` | +3 attenuation models, +inner_radius, +falloff_distance; **T11:** +enable_propagation_delay, +speed_of_sound(343), +compute_propagation_delay(distance) helper (0 if disabled/loop/speed≤0/dist≤0/<10ms); **T12:** +source_radius (0 = point source) |
| `runtime/voice_manager.h/.cpp` | Extended attenuation with inner_radius/falloff logic; **T11:** VoiceSlot.pending_start_delay_s, process_frame holds VOICE_TO_PLAY during countdown (wall-clock delta), +set/get_slot_start_delay, is_slot_start_pending, tick_deferred_starts(delta) |
| `runtime/event_dispatcher.h/.cpp` | **T11:** play_event(event, source_position=Vector3(), has_position=false) — computes distance→listener for 3D one-shots, sets slot delay, returns "delay_s" in dict; delay set once post-acquire (no cooldown stacking) |
| `spatial/occlusion_solver.h/.cpp` | **T12:** generate_volume_samples (deterministic Fibonacci+cube-root radial), compute_volumetric<ClearLosFn> (injectable LOS predicate — testable without physics), solve_volumetric (physics wrapper), VolumetricConfig/Result |
| `spatial/spatial_acoustics_engine.h/.cpp` | **T10:** ReverbPool member + `_update_reverb_pool`, `SpatialParams.damping`, ~20 reverb accessors. **T12:** EmitterState.source_radius/max_distance, volumetric occlusion when radius≥min_radius (bands still from spectral solve), air_cutoff driven from source→listener distance, budget-scaled sample count, volumetric/air config setters, reset on register |
| `nodes/utility/symphony_parameter_smoother.h` | +log-domain mode (mode=1) |
| `register_types.cpp` | +AcousticMaterial, +AcousticBody3D, +SpatialAcousticsEngine singleton |
| `SCsub` | +spatial/*.cpp |

### GDScript (game-template/addons/symphony_audio/)
| File | Change |
|------|--------|
| `audio_manager.gd` | +_players_3d pool, +play_event_3d, +SpatialAcousticsEngine calls (update, register/unregister, spatial param driving, physics space), pool sizes = project settings |
| `sound_event_player_3d.gd` | New — Node3D wrapper with position tracking |

---

## Current Data Flow (end of S5 — Task 12)

```
AudioManager._process(delta):
  SpatialAcousticsEngine.update(delta):
    - advance probe_cache time
    - build EmitterInfo[] from active emitters (distance, audible)
    - scheduler.schedule() → budgeted list of emitters to probe this frame
    - compute _volumetric_samples_this_frame from remaining ray budget
    - for each scheduled emitter:
        _solve_occlusion_for_emitter():
          • spectral solve → target.transmission[3] (drives occlusion LPF)
          • if source_radius ≥ min_radius: volumetric solve → target.occlusion (graduated)
            else: target.occlusion from spectral solve (binary)
          • air_cutoff from source→listener distance (distance_to_air_cutoff)
        _solve_room_for_emitter() → cache lookup OR Fibonacci fan → target.rt60, reverb_send, damping
    - for ALL active emitters: IIR smooth → SeqLock publish
    - _update_reverb_pool(delta): assign emitters to pool slots by smoothed RT60, advance crossfades
  _derive_and_apply_all_voice_parameters(delta):
    - for 3D spatial voices: read compute_occlusion_cutoff/air_cutoff/spatial_gain
      → playback.set_parameter(spatial_occlusion_cutoff / spatial_air_cutoff / spatial_gain)
```

**SpatialParams fields all populated:** attenuation, occlusion (graduated when source_radius>0),
transmission[3], air_cutoff (distance-driven — T12), reverb_send, rt60, damping (T10), apparent_position.
`delay_s` is unused by the DSP path — propagation delay is realized as a VoiceSlot start countdown (T11),
not through SpatialParams.

---

## Task 10: Shared Reverb Pool — ✅ DONE (C++ manager side)

**What shipped (`modules/symphony/spatial/reverb_pool.{h,cpp}` + engine integration):**
- `ReverbPool` (pure C++ data, no Godot Object) with `MAX_SLOTS=8`. Config: `active_slots`, `rt60_cluster_threshold` (0.4s), `crossfade_seconds` (0.25s), `max_rt60` (10s).
- `SlotParams` POD `{room_size, decay_time, damping, pre_delay_ms, wet_gain}` — 5 contiguous floats, exposed via `slot_param_block()` for future GPU offload (asserted `sizeof==5*float`).
- Clustering by RT60 proximity: `assign(emitter, rt60, damping, reverb_send)` → nearest occupied slot within threshold, else a free slot, else **degrade to nearest** (never allocates, never churns buses). Provisional occupancy set inside `assign()` so multiple assigns in the same frame cluster correctly before `update()` recomputes.
- Migration crossfade: on slot change, `prev_slot` fades out over `crossfade_seconds` while `slot` fades in; handles the migrate-back case by inverting the crossfade. `update(delta)` advances crossfades and recomputes weighted-mean per-slot `decay_time`/`damping`/`room_size`.
- Anti-regression (R4): `touched_audio_server()` stays `false` for the pool's lifetime — asserted in tests.
- Integrated in `SpatialAcousticsEngine`: constructor sizes the pool from `audio/symphony/reverb_pool_slots` (platform default 8 desktop / 4 mobile / 2 web via `WEB_ENABLED`/`ANDROID|IOS_ENABLED` macros). `_update_reverb_pool()` runs each frame after smoothing/publish, feeding smoothed `rt60`/`damping`/`reverb_send`. `release()` on `unregister_emitter`.
- `SpatialParams` gained a `damping` field (from `RoomEstimator.high_band_absorption`, stored in `_solve_room_for_emitter`, smoothed, cached in `ProbeCache::RoomProbeResult`).
- ~20 GDScript accessors bound: `set/get_reverb_pool_slots`, `set/get_reverb_pool_enabled`, `get_reverb_slot_{decay_time,damping,room_size,wet_gain}`, `get_emitter_reverb_{slot,send,prev_slot,prev_send}`, `is_emitter_reverb_migrating`, `get_reverb_{active_slot_count,assigned_emitters,migrations,degraded_assignments}`, `reverb_pool_touched_audio_server`.

**Tests:** `tests/modules/test_symphony_spatial_reverb.cpp` — 12 cases / 113 assertions, all pass. Covers clamp, anti-regression, share-cluster, distinct-slots, param derivation, contiguous block, first-snap, monotonic crossfade + completion, degrade-to-nearest, release/reuse, no-spurious-migration, reset.

**REMAINING for Task 10 (GDScript, game-template repo — NOT done here):**
Option (b) audio routing. In `addons/symphony_audio/audio_manager.gd`:
- `_ready()`: create N persistent reverb send buses ONCE (`AudioServer.add_bus` + one `AudioEffectReverb`/Symphony FDN per bus). Never add/remove buses after this. N = `SpatialAcousticsEngine.get_reverb_pool_slots()`.
- Per frame (after `SpatialAcousticsEngine.update`): for each slot, push `get_reverb_slot_decay_time/damping/room_size/wet_gain` into the corresponding bus effect params. For each 3D voice, read `get_emitter_reverb_slot/send` (+ `get_emitter_reverb_prev_slot/prev_send` while `is_emitter_reverb_migrating`) and set the voice's bus-send levels.
- Add a GdUnit4 test asserting bus count is constant across room transitions (mirrors the C++ `touched_audio_server()` guard at the routing layer).

---

## Task 11: Propagation Delay — ✅ DONE (C++ engine side)

**What shipped:**
- `SoundEvent` (`runtime/sound_event.{h,cpp}`): `enable_propagation_delay` (bool, default false), `speed_of_sound` (float, default 343 m/s), and `float compute_propagation_delay(distance)` → `distance / speed_of_sound`, returning **0** when propagation is disabled, the event loops, `speed_of_sound ≤ 0`, `distance ≤ 0`, or the result is below `PROPAGATION_MIN_DELAY_S = 0.01f` (~10 ms). Bound + editor properties (`enable_propagation_delay`, `speed_of_sound` with `suffix:m/s`).
- `SymphonyVoicePool` deferred start (no SceneTreeTimer, no per-play alloc): `VoiceSlot.pending_start_delay_s` lives in the pre-allocated slot. `process_frame()` computes a wall-clock delta (`last_process_usec`), calls `tick_deferred_starts(delta)`, and the `VOICE_TO_PLAY` case **holds** the slot (counts as active, not reclaimable) until the countdown reaches 0, then transitions to `VOICE_PLAYING`. `_activate_slot` and immediate `release_slot` reset the delay (no leak / no stale carry-over). New bound API: `set_slot_start_delay`, `get_slot_start_delay`, `is_slot_start_pending`, `tick_deferred_starts` (the last exposed for deterministic delta control + tests).
- `SymphonyEventDispatcher::play_event(event, source_position = Vector3(), has_position = false)`: for 3D one-shots with propagation enabled it computes `distance = source_position.distance_to(pool->get_listener_position())`, sets the slot delay once (right after acquire — no cooldown stacking), and returns `"delay_s"` in the result dict. Old `play_event(event)` calls still work (defaults → non-positional, delay 0).

**Tests:** `tests/modules/test_symphony_propagation.cpp` — 12 cases / 57 assertions, all pass. Covers: delay = distance/speed, sub-threshold→0, disabled→0, loop→0, non-positive speed/distance→0, TO_PLAY held then transitions on process_frame, zero-delay immediate, release of a pending slot doesn't leak, dispatcher sets delay for distant 3D one-shot, no-position→0, loop→0 via dispatcher, and re-trigger within cooldown does not stack delay.

**REMAINING for Task 11 (GDScript, game-template repo — NOT done here):**
In `addons/symphony_audio/audio_manager.gd::play_event_3d`, pass the position through and honor the returned delay:
- Call `SymphonyEventDispatcher.play_event(event, position, true)` (currently called with just `event`).
- Read `delay_s = dispatch.get("delay_s", 0.0)`. If `delay_s > 0`, **defer `player.play()`** by `delay_s` seconds instead of playing immediately. Cleanest: keep the `AudioStreamPlayer3D` configured (stream/pitch/volume/bus/position set) but hold `play()` until `SymphonyVoicePool.is_slot_start_pending(slot)` becomes false — poll it in the per-frame `_process` loop that already drives the pool, and start the player the frame the countdown clears. Do NOT use a SceneTreeTimer (matches the C++ no-alloc intent).
- The C++ pool already keeps the slot "active" while pending, so voice accounting/attenuation stay correct during the silent lead-in.

## Task 12: Volumetric Occlusion — ✅ DONE (C++ engine side)

**What shipped:**
- `SoundEvent.source_radius` (float, default 0 = point source) + binding + `source_radius` property (`suffix:m`).
- `OcclusionSolver::generate_volume_samples(count, out)` — deterministic sphere-VOLUME fill: Fibonacci spiral (golden-angle azimuth, `acos` polar) × cube-root radial schedule `r = ((i+0.5)/N)^(1/3)` so points are volume-uniform (no RNG, stable frame-to-frame).
- `OcclusionSolver::compute_volumetric<ClearLosFn>(source, radius, listener, sample_count, min_radius, clear_los)` — the core graduated-occlusion algorithm, decoupled from physics via an injectable line-of-sight predicate. For each sample: (1) validate visible from the source centre (reject buried samples), (2) test listener visibility; `occlusion = 1 - visible/valid`. Point source (`radius < min_radius`) or no valid samples → fall back to a single centre→listener ray (graceful degrade to binary). Returns `{occlusion, samples_taken, samples_visible, rays_issued}`.
- `OcclusionSolver::solve_volumetric(space, ...)` — thin physics wrapper that supplies a `PhysicsServer3D` ray LOS predicate to `compute_volumetric`.
- Engine integration: `EmitterState.source_radius`/`max_distance`; `_solve_occlusion_for_emitter` runs volumetric occlusion when `source_radius ≥ min_radius` (per-band transmission still comes from the direct-path spectral solve, which drives the occlusion LPF), and now **wires `air_cutoff` from the source→listener distance** via `SpatialGraphWrapper::distance_to_air_cutoff` (uses per-emitter `max_distance` if set, else `air_absorption_max_distance`, default 100 m). Sample count is budget-scaled each frame (`_volumetric_samples_this_frame` = remaining ray budget / scheduled emitters / 2, clamped [2, configured]). New bound API: `set/get_emitter_source_radius`, `set/get_emitter_max_distance`, `set/get_volumetric_occlusion_enabled`, `set/get_volumetric_sample_count`, `set/get_air_absorption_enabled`, `set/get_air_absorption_max_distance`. Fields reset on `register_emitter`.

**Tests:** `tests/modules/test_symphony_volumetric.cpp` — 11 cases / 122 assertions, all pass. Uses the injectable LOS predicate (no live PhysicsServer3D — direct server stepping SIGSEGVs in the headless doctest harness; the physics wrapper is a thin pass-through so the math coverage is what matters). Covers: source_radius round-trip, volume-uniform + deterministic sample generation, point-source single ray, open→0, fully blocked→1, partial→~0.5 (graduated), buried-samples→centre fallback, budget cap (≤2 rays/sample), null-space audible, air-cutoff falls with distance.

**REMAINING for Task 12 (GDScript, game-template repo — NOT done here):**
In `addons/symphony_audio/audio_manager.gd::play_event_3d`, after `SpatialAcousticsEngine.register_emitter(slot)`:
- `SpatialAcousticsEngine.set_emitter_source_radius(slot, event.source_radius)`
- `SpatialAcousticsEngine.set_emitter_max_distance(slot, event.max_distance)` (so air absorption normalizes against the event's own falloff).
The occlusion LPF + air LPF are already driven per-frame via the existing `compute_occlusion_cutoff` / `compute_air_cutoff` → `set_parameter` path (no new wiring needed there — Task 7 established it; T12 just fills `air_cutoff` with a real distance-based value instead of the 20 kHz default).

---

## TSan Validation (S5 gate — ✅ PASSED)

S5 is code-complete and TSan-validated. ThreadSanitizer build (`bin/godot.macos.editor.arm64.san`)
run over the full symphony suite: **90/90 test cases, 195,220 assertions, 0 failed, and zero
ThreadSanitizer warnings / data races** (`halt_on_error=1`; `grep -i ThreadSanitizer` → no matches).
This exercises the SeqLock publish/read boundary (Task 5), the reverb pool, the propagation-delay
countdown, and the volumetric solver. Re-run any time with the commands below.

---

## Build Commands

```bash
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'

# TSan (S5 gate — PASSED; re-run after new concurrent code)
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=1 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --source-file='*symphony*'
```

---

## Gotchas Learned This Session

- Godot math constants are `Math::PI` / `Math::TAU` (NOT `Math_PI` / `Math_TAU`).
- Physics ray types are `PS3DT::RayParameters` / `PS3DT::RayResult` (from `servers/physics_3d/physics_server_3d_types.h`). `PS3DT` = `#define PhysicsServer3DTypes`.
- `intersect_ray(params, result)` returns bool; result has `.position`, `.normal`, `.collider_id`, `.rid`.
- Physics space accessed via `PhysicsServer3D::space_get_direct_state(RID)`; the RID comes from GDScript `World3D.get_space()` passed to `SpatialAcousticsEngine.set_physics_space()`.
- `ObjectID`/raw pointer returns don't bind cleanly to GDScript but work fine for C++-internal use (occlusion/room solvers call AcousticBody3D::lookup_material directly).
- (T11) `SymphonyVoicePool::process_frame()` takes no delta; it derives one from `OS::get_ticks_usec()` between calls (`last_process_usec`, first call → 0). The pending-start countdown is factored into `tick_deferred_starts(delta)` so tests can drive it deterministically without sleeping.

---

## What's Next

**Phase S5 is code-complete and TSan-validated.** Two tracks of remaining work:

### A. Cross-repo GDScript integration (`game-template/addons/symphony_audio/`) — NOT started
This is what makes S5 audible end-to-end. Nothing below is wired yet; each C++ API exists and is bound.
1. **Reverb routing (T10, option b):** in `audio_manager.gd::_ready()` create N persistent reverb send
   buses ONCE (N = `SpatialAcousticsEngine.get_reverb_pool_slots()`); per frame push
   `get_reverb_slot_{decay_time,damping,room_size,wet_gain}` into each bus effect and set per-voice
   sends from `get_emitter_reverb_slot/send` (+ prev_slot/prev_send while migrating).
2. **Propagation delay (T11):** call `SymphonyEventDispatcher.play_event(event, position, true)` in
   `play_event_3d`; if `delay_s > 0`, hold `player.play()` until `is_slot_start_pending(slot)` clears
   (poll in the existing per-frame loop — no SceneTreeTimer).
3. **Volumetric/air (T12):** after `register_emitter(slot)`, call
   `set_emitter_source_radius(slot, event.source_radius)` and
   `set_emitter_max_distance(slot, event.max_distance)`. The occlusion/air LPF param path already exists.
4. Add GdUnit4 integration tests (real SceneTree): reverb bus-count-constant assertion (T10 R4),
   distant one-shot arrival (T11), real-collider graduated occlusion (T12 — see Deferred item 2).

### B. Phase S6 — Portal Propagation & Reflections (Task 13+), `modules/symphony/`
Next C++ milestone. Start with Task 13 (AcousticRoom3D/AcousticPortal3D authoring nodes + gizmos),
then Task 14 (portal graph bake + Dijkstra path solve), Task 15 (portal routing + diffraction EQ),
Task 16 (shoebox early reflections). See `Spatial_Acoustics_for_Symphony_Plan.md` §Phase S6.

### Resume checklist
1. Read this file + `Spatial_Acoustics_for_Symphony_Plan.md` (Phase S6 section).
2. `git checkout features/symphony_fixed` && `git status`.
3. Rebuild editor; confirm 90 test cases green (`--source-file='*symphony*'`).
4. Also review the ⚠️ Deferred Follow-Ups near the top before shipping S5 to production.
