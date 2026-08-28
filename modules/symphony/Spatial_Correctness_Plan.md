# Spatial Acoustics — Correctness, Modelling & Integration Pass

## Context

Phases S5 and S6 of `Spatial_Acoustics_for_Symphony_Plan.md` are recorded as complete in
`modules/symphony/spatial/HANDOFF_NOTES.md`, with 126/126 C++ tests green (verified: 195,343
assertions, exit 0). A review of the implementation against the plan found that the green suite
covers the S1–S4 operators plus the newer *pure-math* helpers, while the core spatial solvers —
`OcclusionSolver::solve`, `RoomEstimator::estimate`, `ProbeScheduler`, `ProbeCache`,
`AcousticMaterial`, `AcousticBody3D`, and the `SymphonySeqLock`/smoothing path — have **no tests at
all**. Behind that gap sit four shipped-but-silent features and a set of physical-modelling errors:

- `SpatialParams.occlusion` is computed, smoothed and published, and read by **nothing** — Task 12
  is inaudible.
- The alternating ray march double-traverses every barrier, so one solid wall resolves to
  `transmission²` (~16 dB too much attenuation for concrete).
- 3D emitter positions are written once at spawn — moving sounds neither pan nor re-solve.
- The listener's own body is never excluded from occlusion rays.
- `air_cutoff` ratchets monotonically downward and can never recover.
- One escaped ray inflates the room-volume integral by ~130,000 m³, so a room with a window
  computes RT60 ≈ 5.4 s.
- The ray budget bills 8 rays/emitter against an actual ~51, so R7 is ~6× overshot.

Outcome: S5 and S6 genuinely match the plan document — physically defensible, tested at the
solver level, bounded per R7, and audible end-to-end through `game-template`.

**Decisions taken** (all confirmed with the user): blend volumetric occlusion into the transmission
bands (Steam Audio's model); forward-only ray march with `sqrt`; `AcousticRoom3D` becomes `Node3D`;
physical distance-absolute air absorption with an artistic scale knob; aperture-at-boundary RT60 with
solid-angle renormalization and `ignore_floor` off; the engine pulls positions from
`SymphonyVoicePool`; a unified cost-billed ray budget. Full scope: fix + complete + G7 integration.

---

## Phase 1 — Testability unblock

Do this first: it is what catches regressions in every later phase.

**1.1 `AcousticRoom3D` → `Node3D`** (`spatial/acoustic_room_3d.{h,cpp}`)
Base class change only; membership is already a pure OBB test (`point_in_box`) that never used
`Area3D`. Verified zero `.tscn`/`.gd` references in either repo, so this is free now and will not be
later. Drop the `scene/3d/physics/area_3d.h` include; the `set_room_priority` naming workaround for
the `Area3D::set_priority` collision can revert to `set_priority`/`get_priority` (keep the current
names to avoid churn — note the reason is gone). Removes the headless SIGSEGV that blocks all S6
engine-level tests.

**1.2 Templatize the two physics-bound solvers on an injectable raycast functor.**
Mirror the split that already exists at `spatial/occlusion_solver.h:88-152`
(`compute_volumetric<ClearLosFn>` + thin `solve_volumetric` wrapper) — reuse that exact shape:

- `OcclusionSolver::compute<RaycastFn>(source, listener, config, fn)`, with
  `solve(space, ...)` becoming the `PhysicsServer3D` wrapper.
- `RoomEstimator::compute<RaycastFn>(probe_pos, config, fn)`, with `estimate(space, ...)` the wrapper.

The functor returns the hit **and its material** —
`bool fn(const Vector3 &from, const Vector3 &to, Vector3 &r_pos, AcousticMaterial **r_mat)` — so
tests hand back synthetic `AcousticMaterial` refs without an `AcousticBody3D` registry or a live
scene tree. The physics wrappers supply `AcousticBody3D::lookup_material(collider_id)`.

---

## Phase 2 — Blockers

**2.1 Wire volumetric occlusion into the transmission bands**
(`spatial/spatial_acoustics_engine.cpp:~340-360`)
On the volumetric branch **only** (a point source's `result.occlusion` is already
`1 - mean_transmission`, so blending it back would double-apply):

```cpp
const float occ = vres.occlusion;
for (int b = 0; b < 3; b++) {
    e.target.transmission[b] = (1.0f - occ) + occ * result.transmission[b];
}
e.target.occlusion = occ;
```

No graph change needed — `compute_occlusion_cutoff()` and `compute_spatial_gain()` both already
derive from `transmission[]`, so occlusion starts driving level *and* timbre together.
Add `float material_transmission[3]` to `SpatialParams` so the Task 18 overlay can still show the
raw material value (56 → 68 bytes, well under the 256-byte SeqLock cap).

**2.2 Forward-only ray march** (`spatial/occlusion_solver.cpp:30-135`)
Delete `backward_pos` and the `forward = (step % 2 == 0)` alternation; march `p_source` → `p_listener`,
advancing `ray_offset` past each hit. Keep `sqrt` when `hit_count > 1`, keep the
`hit_dist_from_source >= total_distance` guard. Correct for thin plane (1 hit → `t`), thin box
(2 → `t`), N thick walls (2N → `tᴺ`). Halves ray cost. `max_hits = 8` now means "up to 4 barriers".
**Expect to retune the 12 `.tres` presets** — their current values were implicitly tuned against the
squared behaviour, so walls will sound markedly more transparent until they are.

**2.3 Engine pulls emitter state from `SymphonyVoicePool`**
(`spatial/spatial_acoustics_engine.cpp`, `runtime/voice_manager.h`)
`SymphonyVoicePool` is a singleton (`voice_manager.h:137`) whose `VoiceSlot` already carries
`position`, `attenuation_volume`, `max_distance`, `spatial_mode` and `state` (`:54-76`), and
`SoundEventPlayer3D._process` already keeps `position` current. Add const accessors
(`get_slot_position`, `get_slot_attenuation`, `get_slot_max_distance`, `is_slot_audible`) and have
`update()` refresh each emitter from the pool at the top of the frame. `spatial/` already includes
across directories (`spatial_graph_wrapper.h` → `../stream/`) and `runtime/` never includes
`spatial/`, so there is no cycle.

This single change fixes five things: frozen spatial solves for moving emitters; the stale
`source_position` on a recycled emitter index; the hardcoded `info.audible = true` and
`info.importance = 1.0f` in the scheduler feed (`spatial_acoustics_engine.cpp:120-121`); and makes
`set_emitter_max_distance` redundant. Keep `set_emitter_position` bound as an override for
non-pooled callers.

**2.4 Listener body exclusion + panner position** (`game-template/addons/symphony_audio/audio_manager.gd`)
`set_listener_body_rid()` has no caller anywhere. Call it from the listener resolution block
(`audio_manager.gd:935-946`) with the player body's RID. In the same per-frame 3D loop, write
`player.global_position` from the engine's apparent position — this is the panner half of 2.3.

---

## Phase 3 — Correctness bugs

**3.1 Kill the `air_cutoff` ratchet.** Add `float air_cutoff_base` to `EmitterState` (an
intermediate, not a `SpatialParams` field). The occlusion solve writes it (or 20000 when air
absorption is off); `_solve_portals_for_emitter` **assigns** `target.air_cutoff = MIN(air_cutoff_base,
diff_cut)` fresh instead of folding `MIN` into the previous frame's value
(`spatial_acoustics_engine.cpp:322`). Same reset discipline already applied to `portal_gain`.

**3.2 Decouple room estimation from occlusion** (`spatial_acoustics_engine.cpp:96-150`).
Fetch `space` when `occlusion_enabled || room_estimation_enabled`; run the scheduler whenever
`space` is valid; gate each solver independently inside the scheduled loop.

**3.3 Escaped-ray aperture at the room boundary** (`spatial/room_estimator.cpp:78-120`).
Two-pass over a fixed-size stack buffer (`ray_count` ≤ 128 — no heap): pass 1 records hit distances
and materials; compute `mean_hit_distance` from the hits; pass 2 assigns escaped rays
`distance = mean_hit_distance` with `α = 1.0` (the opening is a perfect absorber *at the wall*, not
100 m away). All-escaped → `openness = 1`, `rt60 = 0` directly. An opening now shortens RT60, as it
must.

**3.4 Renormalize the solid angle; `ignore_floor` defaults false**
(`room_estimator.cpp:44`, `room_estimator.h:27`). Count filtered directions up front and use
`4π / active_rays`. With `ignore_floor` off the integral is complete; the knob stays available and is
now at least self-consistent when enabled.

**3.5 Frame-rate-independent smoothing** (`spatial_acoustics_engine.cpp:465`). The comment already
specifies the right formula; the code ignores `p_delta`:
`const float a = 1.0f - Math::pow(1.0f - smooth_alpha, p_delta * 60.0f);`

**3.6 Reverb slot `wet_gain` ramps** (`reverb_pool.cpp:223,232`). Ramp toward 1.0/0.0 at
`1/crossfade_seconds` per second instead of hard-switching — the comment already claims this. A slot
losing its last member currently truncates its tail.

**3.7 Equal-power migration crossfade** (`reverb_pool.cpp:250,259`). `send * sqrt(crossfade)` and
`send * sqrt(1 - crossfade)`; linear amplitude on two decorrelated tails dips 3 dB at the midpoint.

---

## Phase 4 — Physical modelling

**4.1 Physical air absorption** (`spatial/spatial_graph_wrapper.cpp:160-172`).
Replace `20000·(1-d/max_distance)²` with a distance-absolute fit to ISO 9613-1 (20 °C, 50 % RH),
where `f_c` is the frequency at which cumulative absorption reaches ~3 dB:

```
f_c = 4000 · (3 / (0.033 · d · scale))^(1/1.7)      clamped to [200, 20000]
```

Sanity points: 10 m → 14.6 kHz, 30 m → 7.6 kHz, 100 m → 3.8 kHz, 300 m → 2.0 kHz.
Add project setting `audio/symphony/air_absorption_scale` (default 1.0, range 0–10) as the artistic
knob. **Decouples air absorption from `SoundEvent::max_distance`** — which defaults to 2000 m
(`runtime/sound_event.h:34`), making today's effect inert by default and over-harsh only for events
with a tightened radius. Document the scale knob prominently or the physical default will read as
broken.

**4.2 Log-domain cutoff interpolation** — the plan's Task 7 requirement, currently honoured only in
`transmission_to_cutoff`:
- `spatial_acoustics_engine.cpp:469` — smooth `log(air_cutoff)`, then `exp`.
- `portal_router.cpp:81` — `p_max_cutoff * Math::pow(p_min_cutoff / p_max_cutoff, t)` instead of
  `Math::lerp`. A 90° bend goes from an inaudible 10.35 kHz to an obvious 3.7 kHz.

**4.3 Reverb clustering hysteresis** (`reverb_pool.cpp:_find_slot_for`). Centroids drift every frame
and `assign()` runs per emitter per frame, so a boundary emitter oscillates and crossfades forever.
Require a candidate to beat the current slot by `rt60_cluster_hysteresis` (default 0.15 s) and
enforce a `min_dwell_seconds` since the last migration.

**4.4 `room_size` from estimated volume** (`reverb_pool.cpp:_rt60_to_room_size`). `rt60/10.0` puts a
typical 1.5 s room at 0.15. `RoomEstimator::Result::volume` and `ProbeCache::RoomProbeResult::volume`
already exist and are discarded — carry volume through to `assign()` and map
`room_size = clamp(cbrt(volume) / reference_dimension, 0.01, 1.0)`.

---

## Phase 5 — Budget, performance, hygiene

**5.1 Unified cost-billed ray budget** (`spatial/probe_scheduler.{h,cpp}`, engine feed).
- `EmitterInfo` gains `estimated_cost`; the engine computes it as
  `occlusion_hits + (predicted cache miss ? active_room_rays : 0) + 2 · volumetric_samples`.
- Add a side-effect-free `ProbeCache::would_hit(pos) const` for the prediction (reuse `lookup`'s
  staleness logic without touching metrics).
- Scheduler accumulates cost against one pool instead of `budget / rays_per_emitter`
  (`probe_scheduler.cpp:17`).
- Solvers return actual ray counts; `scheduler.report_actual_rays(n)` corrects the next frame.
- **Reserve `min_room_probe_budget`** so a crowd of near emitters cannot starve RT60 — this is the
  known cost of the unified pool.
- Drop the now-meaningless `_volumetric_samples_this_frame` derivation
  (`spatial_acoustics_engine.cpp:135-142`) in favour of the scheduler's allocation.

**5.2 Portal pass cost** (`spatial_acoustics_engine.cpp:158-166`). Currently unbudgeted, O(emitters ×
rooms), with `affine_inverse()` per room per query (`acoustic_room_3d.cpp:80`):
- Hoist `find_room_for_point(listener_position)` out of the per-emitter loop — once per frame.
- Cache per-room inverse transforms in an engine-side array rebuilt only on `registry_epoch` change.
- Implement the membership cache the header already documents (`acoustic_room_3d.h:38-42`) but never
  built: per-emitter `last_room` + epoch, re-tested with one `contains_point` before falling back to
  the full scan.
- Re-solve a portal path only when `(src_room, listener_room)` changes or the portal epoch bumps.

**5.3 Split the epochs** (`acoustic_room_3d.cpp:27`, `acoustic_portal_3d.cpp:27`).
`NOTIFICATION_TRANSFORM_CHANGED` currently bumps the same epoch as add/remove, so an
`AcousticPortal3D` parented to a swinging door — the obvious authoring pattern — rebuilds the whole
graph and flushes the path cache **every frame**. Separate `registry_epoch` (topology → full rebuild)
from `transform_epoch` (movement → refresh edge weights and portal centres in place, path cache stays
valid). Only `set_open()` and topology changes flush the cache.

**5.4 Remove per-frame heap allocation** from the path the plan's Task 5 test calls allocation-free:
- `generate_fibonacci_sphere` / `generate_volume_samples` are deterministic for a given count —
  cache the tables once instead of re-resizing a `Vector` per solve.
- Engine-owned scratch `LocalVector`s for `hops` and `total_deviation`'s `pts`.
- Engine-owned Dijkstra workspace instead of 4 `LocalVector`s per cache miss.
- Bound `PortalPathCache` with LRU eviction (currently unbounded, O(rooms²)).
- Note: `ray_params.exclude.insert()` allocates per ray once 2.4 populates `exclude_rids` — hoist the
  exclude set construction out of the ray loops.

**5.5 Small fixes.**
- `probe_cache.cpp:57-59` — `invalidate_all()` zeroes `total_entries` before adding it to `evictions`.
- `reverb_pool.cpp` — make `audio_server_touched` survive `reset()`, so `set_reverb_pool_slots()` at
  runtime cannot silently re-arm the R4 guard.
- Scheduler: select the top-N by priority rather than insertion-sorting all 256 then round-robining
  over a re-ordered list (the cursor indexes into a different ordering each frame, so fairness is not
  actually guaranteed).

---

## Phase 6 — Complete the authored surface

Everything below is bound, inspector-visible, and read by nothing:

| Field | Wiring |
|---|---|
| `AcousticRoom3D::material`, `reverb_preset_override` | Authored-overrides-estimate in `_solve_room_for_emitter` (handoff deferred item #2) |
| `AcousticRoom3D::shoebox_dimensions` / `has_authored_shoebox()` | Feed `SymphonyEarlyReflections::compute_shoebox_reflections`, falling back to the Task 9 estimate — Task 16 is built, tested and registered (`register_types.cpp:150`) but never instantiated |
| `AcousticPortal3D::transmission_override` | Apply when a closed portal is the only route (Task 15's "closed portals fall back to transmission-only") |
| `AcousticMaterial::total_absorption_transition_speed` | Read into `Result` at `occlusion_solver.cpp:92` then discarded — use it as a per-emitter smoothing override when `total_absorption_hit` |
| `AcousticPortal3D::closest_point_on_aperture` | Use instead of `get_world_center()` for `apparent_position` on wide apertures |
| `AcousticMaterial::scattering` | Leave unused; document as reserved for future reflections |

**Also:** fold `1 / aperture_area` into the portal edge weight. The handoff calls the room-centre
proxy "fine for *which* rooms to route through", but two portals between the same room pair (a wide
arch and a small side door) score almost identically, so Dijkstra can pick the wrong one — and
`apparent_position` is defined as *the last portal on the chosen path*, so a selection error points
the panner at the wrong doorway.

**And:** hold `PortalPathSolver` by pointer in the engine (`spatial_acoustics_engine.h:135` holds a
concrete `DijkstraPathSolver` by value), so the plan's baked-probe backend can substitute without a
header edit.

---

## Phase 7 — Tests

New files under `tests/modules/`, all using the Phase 1.2 injectable functor — no live
`PhysicsServer3D`, no SceneTree:

| File | Key cases |
|---|---|
| `test_symphony_occlusion.cpp` | **Regression: solid slab yields `t`, not `t²`.** No-hit → 1.0; thin plane (1 hit); thin box (2 hits); two walls (4 hits → `t²`); `total_absorption` mute; `max_hits` cap; hit-beyond-listener rejected |
| `test_symphony_room_estimator.cpp` | **Regression: one window yields RT60 *shorter* than sealed.** Sealed concrete cube in range; foam cube much shorter; all-escaped ≈ 0; Eyring engages above threshold; solid-angle renormalization with a partial fan |
| `test_symphony_probe_scheduler.cpp` | **Regression: actual billed rays never exceed budget at 200 emitters.** Near serviced more often than far; cache hit bills 0 room rays; room-probe floor honoured |
| `test_symphony_probe_cache.cpp` | Hit/miss/staleness/LRU/`invalidate_near`/eviction counter |
| `test_symphony_acoustic_material.cpp` | 12 presets round-trip through `.tres`; clamping; default == `generic` |
| `test_symphony_acoustic_body.cpp` | Registry lookup, dereg-on-free, nearest-ancestor resolution (headless-safe now that rooms are `Node3D`) |
| `test_symphony_spatial_engine.cpp` | SeqLock round-trip under a concurrent reader thread; snap-on-first; **same settle time at 30/60/144 fps**; **`air_cutoff` no-ratchet**; **volumetric blend: `occ=0.5`, `T_mat=0.1` → `T_eff=0.55`** |

Extend `test_symphony_portal.cpp` with real membership, priority overlap and epoch invalidation —
now reachable since `AcousticRoom3D` is `Node3D`.

---

## Phase 8 — G7 integration (`game-template/`)

1. **Positions and listener body** — Phase 2.4, plus `player.global_position` from
   `get_emitter_apparent_position(slot)` so the panner points at the doorway.
2. **Portal gain** — apply `get_emitter_portal_gain(slot)` as its own multiplicative layer; it is
   deliberately *not* folded into `attenuation` (which `VoicePool` owns), so do not double-count.
3. **Propagation-delay countdown** (handoff deferred item #1) — drive `tick_deferred_starts(delta)`
   from `_process` and remove the wall-clock derivation inside
   `SymphonyVoicePool::process_frame` (`runtime/voice_manager.cpp:727-733`). The recorded symptom is
   understated: during a pause `process_frame` is not called at all, so the resume produces one huge
   delta and **every pending voice fires simultaneously**. Also guard against double-ticking, since
   `tick_deferred_starts` is bound to GDScript *and* called internally.
4. **Early reflections per reverb slot** — one `SymphonyEarlyReflections` per pool slot, fed authored
   `shoebox_dimensions` else the Task 9 estimate.
5. **Portal scene + GdUnit4** — rooms, doorway, closed-portal reroute, apparent-position redirect.
6. **Neighbour-room reverb coupling** through open portals.
7. **Tighten the T12 test threshold** in `spatial_acoustics_integration_test.gd`. It asserts
   `compute_occlusion_cutoff < 19000`, but an un-tagged collider yields ≈230 Hz — it passes by two
   orders of magnitude. Keep it as the (valuable) live-`PhysicsServer3D` proof of the T6 spectral
   path, rename it accordingly, and add a real volumetric assertion now that Phase 2.1 gives
   volumetric an audible effect. **Handoff deferred item #2 should be reopened**: the existing test
   exercises the spectral solve, not the volumetric one.

---

## Verification

```bash
# C++ suite — expect 126 + ~55 new cases
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'

# TSan gate — mandatory after Phase 2.3 (new cross-subsystem reads) and Phase 5.2
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=1 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --source-file='*symphony*'

# Cross-repo integration
cd /Users/luong.pham/Work/game-template
export GODOT_BIN=/Users/luong.pham/Work/godot/bin/godot.macos.editor.arm64
./addons/gdUnit4/runtest.sh --godot_binary "$GODOT_BIN" \
  -a res://test/addons/symphony_audio/spatial_acoustics_integration_test.gd -c --ignoreHeadlessMode
```

Beyond the suites:

- **Scaling benchmark** (plan's stated verification): frame time *and measured* ray count at
  10/50/100/200 emitters, re-baselined after Phase 5.1 — the current numbers measure a budget that is
  not being enforced.
- **Listening pass on the 12 `.tres` presets** after Phase 2.2. Walls become substantially more
  transparent; presets were implicitly tuned against the squared behaviour.
- **RT60 walkthrough** — carpeted corridor → concrete hall, plus a room with one open window, which
  must read *shorter* than the same room sealed.
- **Arena budget assertions** re-checked after Phase 6 adds `SymphonyEarlyReflections` instances.
- Confirm `reverb_pool_touched_audio_server()` stays false and `AudioServer.bus_count` stays at
  baseline (R4 anti-regression, already covered).

## Documentation

Update `spatial/HANDOFF_NOTES.md`: mark deferred item #2 reopened, item #1 fixed with the corrected
pause symptom, and record the plan's own deferred-prerequisite audit — the virtual path solver and the
contiguous GPU param block are met, but **Task 7's stereo-capable output path is not** (the wrapper is
a single mono chain delegating panning to `AudioStreamPlayer3D`). Decide explicitly whether to keep
that HRTF prerequisite or drop it from the plan document.