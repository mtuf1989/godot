# Spatial Acoustics Handoff Notes — Resume at Phase 6

**Date:** 2026-08-28 (Correctness Pass — Phases 1–5 complete)
**Branch:** `features/up_symphony`
**Plan source of truth:** `modules/symphony/Spatial_Correctness_Plan.md` (8 phases + Verification + Documentation)
**Prior plan (S5/S6 build):** `modules/symphony/Spatial_Acoustics_for_Symphony_Plan.md`
**Reference addon:** `/Users/luong.pham/Work/spatial_audio_player_3d`

---

## TL;DR for the next session

Phases **1–7 of the Correctness Plan are done, committed, and green**. Three mandatory
TSan gates (after 2.3, after 5.2, and after Phase 7's concurrent SeqLock reader test)
passed with **0 data races**. Phase 6 added no concurrent code (no gate needed). Resume at
**Phase 8 — game-template G7 integration**. Then Phase 9 (final verify + docs).

Working tree is CLEAN in both repos (all phase work committed). `reports/` and
`test/addons/symphony_audio/` show as untracked in game-template — pre-existing artifacts,
not this work.

### Verify you're in a good state before starting Phase 8
```bash
cd /Users/luong.pham/Work/godot
git checkout features/up_symphony && git log --oneline -9   # expect Phase 1..7 commits on top
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'   # expect 182/182, 195815 assertions
```
Cross-repo GdUnit4 (expect 6/6, exit 0):
```bash
cd /Users/luong.pham/Work/game-template
export GODOT_BIN=/Users/luong.pham/Work/godot/bin/godot.macos.editor.arm64
./addons/gdUnit4/runtest.sh --godot_binary "$GODOT_BIN" \
  -a res://test/addons/symphony_audio/spatial_acoustics_integration_test.gd -c --ignoreHeadlessMode
```
TSan (only needed again if you add concurrent code; last run 126/126, 0 races):
```bash
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=1 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --source-file='*symphony*'
```

### Commit protocol (confirmed with user)
Commit at each phase boundary. Build + run the C++ suite after every phase; TSan gate
only after phases that add concurrent/cross-subsystem code. User does NOT want commits
squashed. Cross-repo work IS in scope (game-template too).

### Two items flagged for the USER (notes only — do NOT enact)
1. **Preset listening pass.** All 12 material presets were retuned in Phase 2.2 for the
   corrected forward-only ray march (see below). Values are physically-defensible
   (STL-derived) but need the user's ears. Flagged; user will confirm.
2. **HRTF prerequisite decision.** Phase 9 must document (not enact) whether to drop the
   plan's "Task 7 keeps a stereo-capable output path" HRTF prerequisite. **Recommend
   Option A (drop it)**: the architecture deliberately delegates spatialization to
   `AudioStreamPlayer3D`; the wrapper graph is mono-by-design. True HRTF (HRIR dataset +
   partitioned-convolution `SymphonyConvolver`) is a separate future project that would
   make stereo output a deliberate part of *its* design. Leave the final call to the user.

---

## Commit map (branch `features/up_symphony`)

| Commit | Phase | Repo |
|--------|-------|------|
| `bc00fef2b7` | 1 — Testability unblock | godot |
| `7de414315f` | 2 — Blockers | godot |
| `d90fb0c` | 2.4 — listener body + panner | game-template |
| `dd52c6232d` | 3 — Correctness bugs | godot |
| `13e82360a1` | 4 — Physical modelling | godot |
| `e8b1e68f53` | 5 — Budget/perf/hygiene | godot |
| `c9a0087c0b` | 6 — Complete the authored surface (map hash updated in a follow-up commit) | godot |
| `(this commit)` | 7 — New C++ solver tests (+ extended portal) | godot |

**Test baseline:** 182 C++ cases / 195,815 assertions, 0 failed (was 126/195,348 before Phase 7's
56 new cases). game-template GdUnit4 6/6. TSan: 182/182, 0 races.

---

## What changed in Phases 1–5 (so Phase 6 doesn't re-break it)

### Phase 1 — Testability unblock (`bc00fef2b7`)
- **`AcousticRoom3D` is now `Node3D`** (was `Area3D`; include is `scene/3d/node_3d.h`).
  Removed the headless SIGSEGV that blocked S6 engine tests. `set_room_priority`/
  `get_room_priority` names KEPT (the `Area3D::set_priority` collision that forced them is
  gone, but kept to avoid churn — comment says so).
- **`OcclusionSolver::compute<RaycastFn>(source, listener, config, fn)`** — header-only template;
  `solve(space,...)` is a thin `PhysicsServer3D` wrapper. **`RoomEstimator::compute<RaycastFn>
  (probe_pos, config, fn)`** — same split; `estimate(space,...)` wrapper.
- **Functor signature:** `bool fn(const Vector3 &from, const Vector3 &to, Vector3 &r_pos,
  AcousticMaterial **r_mat)` → true on hit; writes hit pos + material* (nullptr = untagged).
- Material access from the templates goes through free functions defined in the `.cpp`s
  (so the header doesn't need `acoustic_material.h`):
  `occlusion_material_is_total_absorption`, `occlusion_material_transmission`,
  `room_estimator_material_absorption`. **Phase 7 tests call `compute<>` directly with a
  synthetic functor — no live physics/SceneTree needed.**

### Phase 2 — Blockers (`7de414315f` + game-template `d90fb0c`)
- **2.1** `SpatialParams` gained `material_transmission[3]` (RAW material value, for the overlay)
  beside `transmission[3]` (EFFECTIVE). On the **volumetric branch only**,
  `transmission[b] = (1-occ) + occ*result.transmission[b]` — occlusion now drives level AND
  timbre (audible via the existing `transmission_to_cutoff` path). Both smoothed.
- **2.2** `OcclusionSolver::compute<>` is **forward-only** now (march source→listener, advance
  past each hit; solid slab = 2 hits → `sqrt` → `t`). Presets retuned in BOTH
  `acoustic_material.cpp::create_preset()` AND `spatial/presets/*.tres` — **must stay in sync**.
- **2.3** Engine `update()` pulls live emitter state from `SymphonyVoicePool::get_singleton()`
  each frame. **GATED on `get_slot_state(vs) != VOICE_FREE`** so non-pooled callers (tests) that
  use `set_emitter_position()` keep their position — do NOT remove this gate (it fixed a real
  regression where the pull-through clobbered a test emitter's position with (0,0,0)).
  Added VoicePool const accessors: `get_slot_position`, `get_slot_attenuation`,
  `get_slot_max_distance`, `is_slot_audible`. Engine includes `../runtime/voice_manager.h`.
- **2.4** (game-template `audio_manager.gd`) `_update_listener_body_rid()` walks the Camera3D's
  ancestors for a `CollisionObject3D` → `set_listener_body_rid` (cached `_listener_body_rid`).
  3D voices set `player.global_position = get_emitter_apparent_position(slot)`.

### Phase 3 — Correctness bugs (`dd52c6232d`)
- **3.1** `EmitterState.air_cutoff_base` intermediate kills the `air_cutoff` ratchet. Occlusion
  solve writes `air_cutoff_base` (+ seeds `target.air_cutoff`); the portal solve ASSIGNS
  `target.air_cutoff = MIN(air_cutoff_base, diffraction_cutoff)` fresh each frame.
- **3.2** `update()` fetches `space` when `occlusion_enabled || room_estimation_enabled`; scheduler
  runs whenever `space` is valid; occlusion & room solvers gated independently.
- **3.3/3.4** `RoomEstimator::compute<>` is **two-pass** over fixed stack buffers (`MAX_RAYS=128`):
  escaped rays get `distance = mean_hit_distance`, `α = 1.0` (perfect absorber AT the wall) so a
  window SHORTENS RT60; all-escaped → `openness=1, rt60=0`. Solid angle = `4π/active_rays`.
  `Config.ignore_floor` default is now **false**.
- **3.5** `_smooth_params`: frame-rate-independent `a = 1 - pow(1 - smooth_alpha, delta*60)`.
- **3.6** Reverb slot `wet_gain` RAMPS toward target (1 with members, 0 idle) at `1/crossfade_seconds`.
- **3.7** Equal-power migration crossfade: `send * sqrt(crossfade)` / `send * sqrt(1-crossfade)`.

### Phase 4 — Physical modelling (`13e82360a1`)
- **4.1** `SpatialGraphWrapper::distance_to_air_cutoff(distance, scale)` — ISO 9613-1 fit
  `f_c = 4000*(3/(0.033*d*scale))^(1/1.7)` clamp [200,20000]; `scale<=0 || d<=0 → 20000`.
  **DECOUPLED from `max_distance`.** Project setting `audio/symphony/air_absorption_scale`
  (default 1.0, range 0–10); engine `get/set_air_absorption_scale` bound.
- **4.2** Log-domain interpolation: `air_cutoff` smoothed as `exp(a*log(tgt)+b*log(cur))`;
  `PortalRouter::diffraction_cutoff` returns `hi*pow(lo/hi, t)` (geometric — 90° bend ≈ 3742 Hz).
- **4.3** Reverb hysteresis + dwell: `ReverbPool::Config.rt60_cluster_hysteresis` (0.15s),
  `min_dwell_seconds` (0.3s); `EmitterAssignment.time_on_slot` advanced in `update()`, reset on
  migration; an existing emitter only migrates if dwelt AND clearly-better.
- **4.4** `SpatialParams.room_volume` carried from RoomEstimator (via cache + smoothing) into
  `ReverbPool::assign(..., p_volume)`; `_volume_to_room_size = clamp(cbrt(V)/30m, 0.01, 1)`,
  falls back to the RT60 map when volume=0.

### Phase 5 — Budget/perf/hygiene (`e8b1e68f53`) — 2nd TSan gate passed
- **5.1** Unified cost-billed ray budget. `ProbeScheduler::EmitterInfo.estimated_cost`,
  `Config.min_room_probe_budget` (16), `report_actual_rays()` correction carryover.
  `ProbeCache::would_hit(pos) const` (side-effect-free). Engine computes per-emitter est_cost =
  `occlusion max_hits + 2*volumetric_sample_count (if radius≥min) + room ray_count (if !would_hit)`.
  **`_solve_occlusion_for_emitter` and `_solve_room_for_emitter` now RETURN `int` (rays issued)**;
  `update()` sums them → `scheduler.report_actual_rays()`. Old `_volumetric_samples_this_frame`
  budget-division derivation dropped (now `CLAMP(config.sample_count, 2, 128)`).
- **5.2** Portal pass: listener room resolved ONCE/frame (`frame_listener_room`/`_node`, hoisted).
  Per-emitter source-room membership cache (`EmitterState.last_src_room_id`/`last_src_node`),
  re-tested via `ObjectDB::get_instance` + `contains_point` before the full scan; invalidated by
  `membership_epoch` on topology change. Path re-solved only on room-pair change / epoch bump.
- **5.3** **Split epochs.** Both `AcousticRoom3D` and `AcousticPortal3D` now have BOTH a topology
  epoch (`registry_epoch` / `state_epoch`) AND a `transform_epoch` (`get_transform_epoch()`).
  `NOTIFICATION_TRANSFORM_CHANGED` bumps `transform_epoch` ONLY. `_rebuild_portal_graph_if_needed()`
  full-rebuilds on `registry^state`; otherwise calls `_refresh_portal_edge_geometry()` which updates
  edge `world_center`/`weight` in place (gated on room^portal `transform_epoch`), no cache flush.
  A portal on a swinging door no longer rebuilds every frame.
- **5.4** `PortalPathCache` bounded with LRU (`Entry{path, last_use}`, `MAX_ENTRIES=256`).
- **5.5** `probe_cache invalidate_all()` counts size before clearing; `ReverbPool::reset()` no
  longer clears `audio_server_touched` (R4 guard can't be re-armed by a runtime reconfigure);
  scheduler selects top-N over the PRIORITY order (fairness actually preserved).

**Deferred within Phase 5 (documented, non-correctness, main-thread only):** the sample-table
memoization (`generate_fibonacci_sphere`/`generate_volume_samples` regen a small `Vector` per
solve) and the Dijkstra/hops scratch-vector reuse were NOT done. They don't affect correctness
or the audio-thread no-alloc guarantee. Pick up in a perf pass if desired.

### Test files touched by Phases 1–5 (existing suites, updated for new physics)
- `tests/modules/test_symphony_spatial_reverb.cpp` — wet_gain ramp (drive ~20 frames);
  `make_pool(slots, cluster, crossfade, hysteresis=0, min_dwell=0)` so migration-mechanics cases
  stay ungated by the new 4.3 hysteresis.
- `tests/modules/test_symphony_portal_router.cpp` — bent-cut expects the geometric mean
  `sqrt(20000*700) ≈ 3742` (log interp), not the linear midpoint.
- `tests/modules/test_symphony_volumetric.cpp` — air-cutoff test uses the `(distance, scale=1.0)`
  signature; asserts monotonic falloff + scale=0 → 20000 + a ~30 m sanity band.

---

## ▶ NEXT: Phase 6 — Complete the authored surface
## ✅ DONE: Phase 6 — Complete the authored surface

All six bound-but-unread fields are now wired, plus the two structural items. Build green
(126/126, 195,348 assertions). No concurrent code added → no new TSan gate. What landed
(all in `modules/symphony/spatial`):

| Field / target | How it was wired |
|---|---|
| `AcousticRoom3D::material`, `reverb_preset_override` | `_solve_room_for_emitter` resolves the emitter's authored room (reusing the `last_src_room_id` membership cache, else one `find_room_for_point`). If `reverb_preset_override` (wins) or `material` is set, RT60 is derived analytically from the room's half-extent **bounds** volume+surface and the material's mean absorption (Sabine < `eyring_threshold`, Eyring above), `damping = absorption_high`, `reverb_send = openness_to_reverb_send(0)`; returns 0 rays. Routed through `target` → `_smooth_params` → publish, so it's smoothed like the estimate. |
| `AcousticRoom3D::shoebox_dimensions` / `has_authored_shoebox()` | New `SpatialParams::shoebox_dimensions` (Vector3) is set from the room each room-solve (zero when unauthored). New bound accessor `get_emitter_shoebox_dimensions(slot)` returns it (from `target`, not smoothed — dims are discrete). Per-slot `SymphonyEarlyReflections` instantiation remains the game-template side (Phase 8.4). |
| `AcousticPortal3D::transmission_override` | New `_apply_closed_portal_transmission(e, src_room, lis_room)` — on an **unreachable** path, scans the portal registry for a CLOSED portal that `connects(src, lis)`; folds its `transmission_override` bands (or default `0.05/0.03/0.015`, or `0` for total-absorption) **multiplicatively** into `target.transmission[]` and raises `target.occlusion`. |
| `AcousticMaterial::total_absorption_transition_speed` | New `EmitterState::smoothing_speed_override`. The occlusion solve sets it from `result.total_absorption_speed` when `total_absorption_hit`, else 0. `_smooth_params` uses `a = 1 - exp(-speed·dt)` when the override > 0, else the global fps-independent alpha. |
| `AcousticPortal3D::closest_point_on_aperture` | `PortalHop` gained `apparent` + `has_apparent`. The engine sets `hop.apparent = portal->closest_point_on_aperture(listener_position)` per hop; `PortalRouter::apparent_position` returns the last hop's `apparent` when set (else `center`). |
| `AcousticMaterial::scattering` | Left unused; documented **RESERVED** for future scattered/late-reflection work in `acoustic_material.h`. |

**Also landed:**
- **`1/aperture_area` folded into the portal edge weight** in BOTH `_rebuild_portal_graph_if_needed()`
  and `_refresh_portal_edge_geometry()`. `PortalEdge` gained an `aperture_area` field; `PortalGraph::add_edge`
  takes it. Penalty = `MAX(PORTAL_REF_AREA / area, 1)` with `PORTAL_REF_AREA = 2.0` (matches
  `PortalRouter::path_gain`'s default ref): a wide arch keeps its pure distance cost, a small door costs more.
- **`PortalPathSolver` held by pointer.** `spatial_acoustics_engine.h` now holds `PortalPathSolver *portal_solver`;
  `memnew(DijkstraPathSolver)` in the ctor, `memdelete` in the dtor, call site is `portal_solver->solve(...)`.
  A baked-probe backend can now substitute without a header edit.

**Phase 6 notes for later phases:**
- `_solve_room_for_emitter` writes `e.last_src_node = -1` on a fresh room resolve — harmless: the portal pass
  re-derives the node index when `< 0`. It shares `last_src_room_id` with the portal pass's membership cache.
- The authored-room fast path runs only for **scheduled** emitters (room solve is in the scheduled loop);
  unscheduled emitters keep last frame's smoothed values, same as the estimate path. Fine.
- `SpatialParams` grew by one Vector3 (shoebox dims); the `static_assert(<= 256)` still holds (compiles).
- **Phase 7 test hooks now available:** the closed-portal fallback, aperture-weighted routing, and the
  smoothing-speed override are all reachable via the pure `compute<>`/router math or the `Node3D`-based
  registry (rooms/portals are headless-safe). `get_emitter_shoebox_dimensions` is bound for GdUnit4.

<details><summary>Original Phase 6 plan (for reference)</summary>

Everything below is bound, inspector-visible, and currently read by NOTHING. Wire each.
(See `Spatial_Correctness_Plan.md` §"Phase 6" for the authoritative table.)

| Field / target | Wiring |
|---|---|
| `AcousticRoom3D::material`, `reverb_preset_override` | Authored-overrides-estimate in `_solve_room_for_emitter`: if the emitter's room has an authored material / preset override, derive RT60/damping/absorption from it instead of (or blended with) the ray-fan estimate. |
| `AcousticRoom3D::shoebox_dimensions` / `has_authored_shoebox()` | Feed `SymphonyEarlyReflections::compute_shoebox_reflections(dims, listener_off, source_off, reflection, speed, out[6])`, falling back to the Task 9 estimate when unauthored. The operator is built + registered (`register_types.cpp`) but **never instantiated** — the per-slot instantiation is the game-template side (Phase 8.4); Phase 6 is the C++ wiring that makes the dims available. |
| `AcousticPortal3D::transmission_override` | Apply when a CLOSED portal is the only route (Task 15's "closed portals fall back to transmission-only"). `AcousticPortal3D::get_transmission_override()` returns a `Ref<AcousticMaterial>` (may be null → small default transmission). |
| `AcousticMaterial::total_absorption_transition_speed` | Read into `OcclusionSolver::Result::total_absorption_speed` (already surfaced), then currently discarded — use it as a per-emitter smoothing-speed override when `total_absorption_hit`. |
| `AcousticPortal3D::closest_point_on_aperture(world_point)` | Use instead of `get_world_center()` for `apparent_position` on WIDE apertures (so the panner points at the nearest part of a big arch, not its centre). |
| `AcousticMaterial::scattering` | LEAVE unused; document as reserved for future reflections. |

**Also in Phase 6:**
- **Fold `1/aperture_area` into the portal edge weight** so Dijkstra distinguishes a wide arch
  from a small side door between the same room pair (else `apparent_position` can point at the
  wrong doorway). Update BOTH `_rebuild_portal_graph_if_needed()` and
  `_refresh_portal_edge_geometry()` (they compute the weight in two places now — 5.3).
  `AcousticPortal3D::get_aperture_area()` exists; `PortalEdge` has no area field yet — either add
  one or fold the term into `weight` at build/refresh time.
- **Hold `PortalPathSolver` by pointer** in the engine. Currently
  `spatial_acoustics_engine.h` holds a concrete `DijkstraPathSolver portal_solver;` by value.
  Change to a pointer/owned-ptr to the abstract `PortalPathSolver` so a baked-probe backend can
  substitute without a header edit. Watch: `portal_solver.solve(...)` call site becomes
  `portal_solver->solve(...)`; construct the default `DijkstraPathSolver` in the ctor, free in dtor.

**Phase 6 gotchas:**
- `AcousticMaterial` getters: `get_absorption_low/mid/high`, `get_transmission_low/mid/high`,
  `get_scattering`, `get_total_absorption`, `get_total_absorption_transition_speed`,
  `get_mean_absorption`.
- The room material path needs the emitter's room. The membership cache from 5.2
  (`last_src_room_id`/`last_src_node`) already resolves it in the portal pass — but
  `_solve_room_for_emitter` runs in the SCHEDULED loop (occlusion/room), which is BEFORE the
  portal pass. You'll need to resolve the emitter's room inside `_solve_room_for_emitter` (reuse
  `AcousticRoom3D::find_room_for_point`, or hoist a shared per-emitter room resolve). Keep it cheap.
- `SpatialParams` is under a 256-byte SeqLock cap (`static_assert`). Current size is well under,
  but check if you add fields.
- Room-material override should still be smoothed like the estimate (goes through `target` →
  `_smooth_params` → publish).

</details>

---

## Remaining phases after 7

### ✅ DONE: Phase 7 — New C++ tests
Added **7 new test files** + extended `test_symphony_portal.cpp`. Suite **126 → 182 cases**
(56 new), **195,815 assertions, 0 failed**. TSan gate PASSED (182/182, 0 races) — required
because `test_symphony_spatial_engine.cpp` spins a concurrent SeqLock reader thread. Per-suite
counts: occlusion 9, room_estimator 8, probe_scheduler 7, probe_cache 9, acoustic_material 7,
acoustic_body 5, spatial_engine 7, portal 33 (+4). All solver tests drive the Phase 1.2
`compute<>` injectable functors — no live `PhysicsServer3D`.

**Key regressions locked in:** solid slab yields `t` not `t²` (occlusion); a window yields RT60
SHORTER than sealed (room estimator); billed rays stay within budget at 200 emitters (scheduler);
`air_cutoff` recovers upward — no monotonic ratchet (engine); fps-independent settle at 30/60/144.

**⚠ Headless-3D limitation discovered (important for Phase 8):** even though `AcousticRoom3D` is
now `Node3D`, the headless doctest harness stands up **no 3D World/RenderingServer**, so ANY
in-tree call that touches `is_inside_tree()+global_transform` (`contains_point`),
`set_global_transform`, or `update_gizmos()` (`set_bounds` while in-tree) **SIGSEGVs** — first
attempt crashed on exactly this. The portal-suite extension therefore verifies the registry/epoch/
priority MODEL through the **tree-free** surface (static setters that bump `registry_epoch`, plus
`point_in_box` priority selection). **Real in-tree room membership / `find_room_for_point` /
`AcousticPortal3D` room resolution must be tested in the live-SceneTree GdUnit4 layer (Phase 8),
NOT in doctest.** `test_symphony_acoustic_body.cpp` follows the same rule: it verifies the static
registry LOOKUP contract only (registration needs an in-tree `CollisionObject3D` parent).

<details><summary>Original Phase 7 plan (for reference)</summary>

- **Phase 7 — Tests (~7 new C++ files, ~55 cases).** All use the Phase 1.2 injectable functor —
  no live `PhysicsServer3D`, no SceneTree. Files: `test_symphony_occlusion.cpp` (regression: solid
  slab yields `t` not `t²`), `test_symphony_room_estimator.cpp` (window → RT60 SHORTER than sealed),
  `test_symphony_probe_scheduler.cpp` (billed rays ≤ budget at 200 emitters), `test_symphony_probe_cache.cpp`,
  `test_symphony_acoustic_material.cpp` (12 presets round-trip .tres; default==generic),
  `test_symphony_acoustic_body.cpp` (headless-safe now rooms are Node3D),
  `test_symphony_spatial_engine.cpp` (SeqLock concurrent reader; fps-independent settle;
  air_cutoff no-ratchet; volumetric blend occ=0.5,T_mat=0.1→T_eff=0.55). Extend
  `test_symphony_portal.cpp` with real membership/priority/epoch. **Register new test files in
  the tests SCsub/build if needed (check how existing `tests/modules/test_symphony_*.cpp` are picked up).**

</details>

- **Phase 8 — G7 integration (game-template `audio_manager.gd` + a new portal test scene).**
  Positions/listener body (2.4 already partly done); portal_gain as its OWN gain layer (do NOT
  double-count with attenuation); **propagation-delay countdown from real delta** — drive
  `tick_deferred_starts(delta)` from `_process` and REMOVE the wall-clock derivation in
  `SymphonyVoicePool::process_frame()` (`runtime/voice_manager.cpp` ~727-733); guard against
  double-ticking (it's bound to GDScript AND called internally). The pause bug is worse than the
  old note said: during a pause `process_frame` isn't called, so resume fires ALL pending voices
  at once. Early reflections per reverb slot; portal scene + live-SceneTree GdUnit4; neighbour-room
  reverb coupling; **tighten the T12 test** (rename to reflect it tests the spectral path; add a
  real volumetric assertion now that 2.1 makes volumetric audible) and **reopen deferred item #2**.
- **Phase 9 — Final verify + docs.** Full C++ suite (126 + ~55 new); TSan gate; cross-repo GdUnit4;
  scaling benchmark note (frame time + MEASURED ray count at 10/50/100/200 emitters — re-baseline
  after 5.1 since the old numbers measured an unenforced budget); update THIS file; record the
  HRTF decision (Option A recommended) without enacting; keep the preset listening pass flagged.

---

## Standing gotchas (still true)
- Math constants: `Math::PI` / `Math::TAU` (NOT `Math_PI`).
- Physics ray types: `PS3DT::RayParameters` / `PS3DT::RayResult`; `intersect_ray(params, result)`
  returns bool with `.position`, `.normal`, `.collider_id`, `.rid`.
- `Area3D` header (if ever needed) is `scene/3d/physics/area_3d.h`. Rooms are Node3D now, so the
  headless doctest harness can construct them — but Phase 7 still prefers the pure `compute<>`
  math path (no scene tree) for speed and determinism.
- `update_gizmos()` guarded behind `is_inside_tree()`.
- When folding a per-frame-recomputed value into a `SpatialParams` target, RESET it at the top of
  the solve (the portal solve resets `portal_gain`/`apparent_position` each frame — follow the
  same discipline for any new per-frame folded value in Phase 6).
- `AudioStreamPlayer3D` plays to exactly ONE bus — no native per-voice aux send. The reverb-slot
  routing tradeoff (a voice on a reverb slot bus loses its dry category-bus volume) is documented;
  the richer-routing accessors (`get_emitter_reverb_prev_slot`/`prev_send`, `is_emitter_reverb_migrating`)
  are bound and available if a custom send effect lands later.
