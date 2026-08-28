# Spatial Acoustics Handoff Notes — Next Session

**Date:** 2026-08-28 (updated after S6 complete + portal routing getters exposed to GDScript)  
**Branch:** `features/up_symphony`  
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
| **Task 13** | Room/portal authoring nodes (AcousticRoom3D, AcousticPortal3D) + editor gizmos + membership | ✅ Done |
| **Task 14** | Portal graph + Dijkstra path solve behind virtual PortalPathSolver + path cache | ✅ Done |
| **Task 15** | Portal routing — apparent_position, aperture/incidence path_gain, DeviationModel diffraction LPF; engine integration + GDScript getters | ✅ Done |
| **Task 16** | Shoebox early reflections — SymphonyEarlyReflections operator (6 image sources) | ✅ Done |

**Phase S5 (Spatial Acoustics Core) — COMPLETE & TSan-validated.**
**Phase S6 (Portal Propagation & Reflections) — COMPLETE & TSan-validated (C++ side).**
Next: G7-style cross-repo integration pass in `game-template/` (consume S6 getters; wire early-reflections per reverb slot; live-SceneTree GdUnit4 tests) + two deferred modelling refinements (see below).

**Tests:** 126/126 pass (195,343 assertions). S5: 12 [Reverb] + 12 [Propagation] + 11 [Volumetric].
S6: 10 [Portal] + 10 [PortalGraph] + 9 [PortalRouter] + 7 [EarlyReflections].
**TSan:** 126/126 pass under ThreadSanitizer, 0 data races (S5 + S6 gates cleared).
**Last build:** ~12s editor arm64, clean (2 pre-existing unrelated warnings).

### ⏭️ Deferred modelling refinements (for the integration pass — NOT bugs)
1. **Portal edge weight is a room-centre proxy** — `_rebuild_portal_graph_if_needed()` weights each
   edge as `roomA_center→portal + portal→roomB_center`, not the true per-emitter source→listener
   routed distance. Fine for *which* rooms to route through; refine if routed propagation delay /
   distance-attenuation needs to track the real path length.
2. **Authored room material / reverb_preset_override unused for RT60** — `AcousticRoom3D.material`
   and `reverb_preset_override` are authored but the engine still derives RT60/reverb solely from
   the Task 9 ray-fan estimate. "Authored overrides estimate" is the intended enhancement.

---

## ✅ Track A — Cross-repo GDScript Integration (DONE 2026-08-28)

S5 is now **audible end-to-end**. All three remaining GDScript wiring items were
implemented in `game-template/addons/symphony_audio/audio_manager.gd` and validated
by a real-SceneTree GdUnit4 suite. Nothing here touched the C++ module.

| Item | What was wired | Status |
|------|----------------|--------|
| **T12** | `play_event_3d`: after `register_emitter`, call `set_emitter_source_radius(slot, event.source_radius)` + `set_emitter_max_distance(slot, event.max_distance)`. | ✅ |
| **T11** | `play_event_3d` dispatches `play_event(event, position, true)`, reads `delay_s`; if delayed, holds `player.play()` (tracked in `_pending_start_slots`) until `is_slot_start_pending(slot)` clears — polled in new `_start_deferred_voices()` in `_process`. No SceneTreeTimer. Cleanup in `_release_slot` + both `_detach_stolen_slot` branches. | ✅ |
| **T10** | `_setup_reverb_buses()` (in `_ready()`) creates N = `get_reverb_pool_slots()` persistent `SpatialReverbK` send buses ONCE (one `AudioEffectReverb` each → Master), records bus-count baseline. `_update_reverb_routing()` (per frame, after `SpatialAcousticsEngine.update`) pushes slot `room_size`/`damping`/`wet_gain` into each bus effect and routes 3D voices to their assigned slot bus. Never adds/removes buses at runtime (R4). | ✅ |

**New public accessors on AudioManager (for tests/diagnostics):** `get_reverb_bus_count_baseline()`, `get_reverb_bus_names()`.

**Tests:** `game-template/test/addons/symphony_audio/spatial_acoustics_integration_test.gd`
(`class SpatialAcousticsIntegrationTest`) — **6/6 pass, 0 errors/failures/orphans, exit 0**:
reverb buses created once; **bus count constant across 6 room transitions + `reverb_pool_touched_audio_server()` false** (T10 R4); distant one-shot schedules delay >0.5s + slot pending, near/disabled → no delay (T11); **real StaticBody3D+BoxShape3D collider between a sized emitter and listener drives `compute_occlusion_cutoff` below 19 kHz** (T12 — this also closes Deferred Follow-Up #2 below).

**Run the suite:**
```bash
cd /Users/luong.pham/Work/game-template
export GODOT_BIN=/Users/luong.pham/Work/godot/bin/godot.macos.editor.arm64
./addons/gdUnit4/runtest.sh --godot_binary "$GODOT_BIN" \
  -a res://test/addons/symphony_audio/spatial_acoustics_integration_test.gd -c --ignoreHeadlessMode
```
(Pre-existing CLI noise — `"!configured"` StringName errors, remote-debugger + `godot_mcp/.../game_helper.gd:23` errors — is unrelated to this work and appears regardless. Suite still reports PASSED / exit 0.)

**⚠️ Design tradeoff to know before shipping reverb (T10):** Godot's `AudioStreamPlayer3D`
plays to exactly ONE bus — there is **no native per-voice aux-send** into a shared bus.
So per-voice reverb *send levels* into a shared slot are not natively expressible, and a
voice routed to a reverb slot bus stops receiving its dry **category-bus volume/mute**
(reverb slot buses send to Master, not to the SFX/Ambient/etc. bus). The current impl is
the faithful, shippable version within this constraint. If true per-voice wet sends or
category-volume-on-reverb matter later, options are: (a) a custom send `AudioEffect`, or
(b) route reverb slot buses into a category bus, or (c) one bus per voice (rejected — the
addon's flaw). The `get_emitter_reverb_send` / `prev_slot` / `prev_send` +
`is_emitter_reverb_migrating` accessors are bound and available if a richer routing lands.

---

## ✅ Phase S6 — Portal Propagation & Reflections (DONE 2026-08-28)

All four S6 tasks landed in `modules/symphony/`. Full symphony suite **126/126 cases,
195,343 assertions, 0 failed**; **TSan gate PASSED** (126/126 under ThreadSanitizer,
0 data races — S6 is all main-thread, SeqLock path unchanged from S5). Editor build clean.

| Task | Scope | Files | Status |
|------|-------|-------|--------|
| **13a** | AcousticRoom3D (Area3D) + AcousticPortal3D (Node3D) authoring nodes, membership + static registries + epochs | `spatial/acoustic_room_3d.{h,cpp}`, `spatial/acoustic_portal_3d.{h,cpp}` | ✅ |
| **13b** | Editor gizmos (room bounds box, portal aperture rect + normal) | `editor/acoustic_gizmos.{h,cpp}` | ✅ |
| **13c** | Membership + portal-state tests (10 cases) | `tests/modules/test_symphony_portal.cpp` | ✅ |
| **14** | Portal graph + Dijkstra behind virtual `PortalPathSolver` + `PortalPathCache` (10 cases) | `spatial/portal_graph.{h,cpp}`, `tests/modules/test_symphony_portal_graph.cpp` | ✅ |
| **15** | Routing: apparent_position→last portal, aperture/incidence `path_gain`, DeviationModel diffraction LPF; engine integration (9 cases) | `spatial/portal_router.{h,cpp}`, `spatial/spatial_acoustics_engine.{h,cpp}`, `tests/modules/test_symphony_portal_router.cpp` | ✅ |
| **16** | SymphonyEarlyReflections operator — shoebox 6 image sources (7 cases) | `nodes/delay/symphony_early_reflections.h`, `tests/modules/test_symphony_early_reflections.cpp` | ✅ |

**Engine integration (Task 15):** `SpatialAcousticsEngine::update()` runs a portal pass
before smoothing (guarded by `portal_propagation_enabled` && graph has rooms).
`_rebuild_portal_graph_if_needed()` rebuilds the abstract graph from the live
AcousticRoom3D/AcousticPortal3D registries when `room_epoch ^ portal_epoch` changes and
flushes the path cache. `_solve_portals_for_emitter()` resolves src/listener rooms, solves
(cached) the path, and folds results into the target: `apparent_position` = last portal,
`portal_gain` = per-hop aperture/incidence gain (its OWN SpatialParams field — NOT folded into
`attenuation`, which VoicePool owns/mirrors), `air_cutoff = MIN(air_cutoff, diffraction_cutoff)`
(min-freq stack, never multiply). `portal_gain` + `apparent_position` are reset to unity/true-pos
at the top of the solve each frame (fixes a compounding bug where the old `attenuation *= pgain`
multiplied every frame with no reset). Bound config: `set/get_portal_propagation_enabled`,
`set/get_portal_diffraction_min_cutoff`, `get_portal_graph_room_count/edge_count`.

**Consumable by GDScript (added 2026-08-28):** `get_emitter_apparent_position(voice_slot)` →
Vector3 (smoothed; equals true source when no portal path) and `get_emitter_portal_gain(voice_slot)`
→ float [0,1] (smoothed). The game-template layer sets the `AudioStreamPlayer3D.global_position`
to the apparent position (panner points at the doorway) and applies `portal_gain` as its own
multiplicative gain layer on top of VoicePool attenuation (no double-count). The diffraction LPF
already flows through the existing `compute_air_cutoff` path.

**Testability note:** the headless doctest harness SIGSEGVs on Area3D physics (add_child /
memdelete of a never-in-tree Area3D — same limitation as S5 T12). So all S6 tests exercise
the **pure math** via static helpers (`AcousticRoom3D::point_in_box`,
`AcousticPortal3D::closest_point_on_rect`, the abstract `PortalGraph`, `PortalRouter::*`,
`SymphonyEarlyReflections::compute_shoebox_reflections`). The engine's live-registry portal
pass is regression-verified via build + full suite; end-to-end room resolution should get a
game-template GdUnit4 test (full SceneTree) like the S5 T12 collider test.

**Gotchas:** Area3D header is `scene/3d/physics/area_3d.h`. `set_priority`/`get_priority`
collide with Area3D — room uses `set_room_priority`/`get_room_priority`. `update_gizmos()`
guarded behind `is_inside_tree()`.

**Deferred to game-template (GDScript, like S5 Track A):** (1) EarlyReflections "one shared
instance per reverb pool slot" bus wiring; (2) portal `AcousticRoom3D` / `AcousticPortal3D`
scene authoring + a live-SceneTree GdUnit4 test for room membership + portal routing;
(3) neighbour-room reverb coupling through open portals (plan Task 15 mentions blending
Task 10 send levels across portals — the C++ apparent-position/gain/diffraction landed;
reverb-send blending across portals is the remaining routing-layer piece).

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

2. **✅ RESOLVED (Track A, 2026-08-28) — Volumetric occlusion now has a real-physics test.**
   Was: driving `PhysicsServer3D` directly in the headless doctest harness SIGSEGVs, so the
   graduated-occlusion math was only tested via the injectable LOS predicate
   (`OcclusionSolver::compute_volumetric<ClearLosFn>`); the physics wrapper
   `solve_volumetric()` was a thin pass-through never exercised by an actual raycast.
   Now: `test_real_collider_produces_occlusion` in
   `game-template/test/addons/symphony_audio/spatial_acoustics_integration_test.gd` runs
   inside a full SceneTree, places a real `StaticBody3D`+`BoxShape3D` wall between a sized
   emitter and the listener, drives 60 engine updates, and asserts `compute_occlusion_cutoff`
   drops below 19 kHz — exercising the live `PhysicsServer3D` ray path end-to-end.

4. **✅ RESOLVED (Track A, 2026-08-28) — Reverb-pool R4 now guaranteed GDScript-side too.**
   `ReverbPool::touched_audio_server()` proves the C++ manager never mutates AudioServer,
   but the actual bus creation is GDScript (plan option b). Now
   `test_bus_count_constant_across_room_transitions` asserts `AudioServer.bus_count` equals
   the post-setup baseline across 6 simulated room transitions AND that
   `reverb_pool_touched_audio_server()` stays false — locking the "create buses once, never
   at runtime" invariant at the routing layer. Buses are created once in
   `AudioManager._setup_reverb_buses()` (`_ready()`) and never added/removed after.

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
- (S6) `Area3D` header is `scene/3d/physics/area_3d.h` (NOT `scene/3d/area_3d.h`).
- (S6) `set_priority`/`get_priority` COLLIDE with Area3D/CollisionObject3D — AcousticRoom3D uses `set_room_priority`/`get_room_priority`.
- (S6) The headless doctest harness SIGSEGVs on Area3D physics: both `add_child` of an Area3D into the SceneTree root AND `memdelete` of a never-in-tree Area3D crash (physics world/RID). Node3D-derived AcousticPortal3D is fine. → S6 tests exercise **pure static math helpers** (`point_in_box`, `closest_point_on_rect`, abstract `PortalGraph`, `PortalRouter::*`, `compute_shoebox_reflections`); guard `update_gizmos()` behind `is_inside_tree()`.
- (S6) When folding a per-frame-recomputed value into a SpatialParams target, RESET it at the top of the solve — the portal solve originally did `target.attenuation *= pgain` with no reset and compounded every frame. `portal_gain` is now its own field, reset to 1.0 each frame.

---

## What's Next

**Phases S5 and S6 are code-complete and TSan-validated. S5 is wired into GDScript (Track A ✅);
S6 is C++-complete with routing outputs exposed to GDScript but NOT yet consumed in-game.**
Remaining work is a single cross-repo **integration pass** in `game-template/`.

### A. S5 GDScript integration (`game-template/addons/symphony_audio/`) — ✅ DONE
See the **Track A** section near the top. T10 reverb routing, T11 propagation delay, T12
volumetric/air wired in `audio_manager.gd`, covered by
`test/addons/symphony_audio/spatial_acoustics_integration_test.gd` (6/6 pass).
Open reverb-polish sub-items (single-bus routing tradeoff; `prev_slot`/`prev_send` unused) — see Track A.

### B. Phase S6 — Portal Propagation & Reflections — ✅ DONE (C++ side)
See the **✅ Phase S6** section near the top for the full file/task breakdown. All routing math,
the portal graph + Dijkstra + cache, engine integration, and the EarlyReflections operator
landed and are tested (36 new cases). Portal outputs are exposed via
`get_emitter_apparent_position` + `get_emitter_portal_gain`.

### C. Next session — S6 game-template integration pass (NOT started)
This makes S6 audible end-to-end (mirrors what Track A did for S5). In `game-template/`:
1. **Consume portal routing** in `audio_manager.gd`: per 3D voice, set the
   `AudioStreamPlayer3D.global_position` to `SpatialAcousticsEngine.get_emitter_apparent_position(slot)`
   (doorway direction), and apply `get_emitter_portal_gain(slot)` as an extra multiplicative gain
   layer (e.g. a new volume-stack layer or fold into the existing spatial gain — do NOT double-count
   with VoicePool attenuation). Diffraction LPF already flows via `compute_air_cutoff`.
2. **Author a portal test scene** (rooms + portal doorway) and add a live-SceneTree GdUnit4 test:
   room membership, apparent-position redirect through the doorway, closed-portal reroute. This also
   covers the S6 engine integration that the headless doctest harness can't (Area3D physics).
3. **Early reflections per reverb slot**: instantiate one `SymphonyEarlyReflections` per reverb pool
   slot and feed it room dimensions (authored `AcousticRoom3D.shoebox_dimensions` else Task 9 estimate).
4. **Neighbour-room reverb coupling** through open portals (blend Task 10 send levels across portals).

### D. Deferred modelling refinements (see "⏭️ Deferred modelling refinements" near the top)
- Portal edge weight is a room-centre proxy (refine to real routed distance if needed).
- Authored room material / `reverb_preset_override` not yet used for RT60 (authored-overrides-estimate).

### Resume checklist
1. Read this file + `Spatial_Acoustics_for_Symphony_Plan.md` (§Phase S6 for context; §Phase G7 for the integration pass).
2. `git checkout features/up_symphony` && `git status` (S5+S6 changes are working-tree edits unless committed).
3. Rebuild editor; confirm **126** test cases green (`--source-file='*symphony*'`).
4. Re-run the Track A GdUnit4 suite (command in the Track A section) — expect 6/6 pass.
5. Start the S6 integration pass (section C above) in `game-template/`.
6. Review the ⚠️ Deferred Follow-Ups + ⏭️ Deferred modelling refinements near the top before shipping to production.
   (only #1 — the wall-clock propagation countdown — remains open; #2 and #4 are now resolved).
