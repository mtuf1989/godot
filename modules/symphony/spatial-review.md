Yes—**several bugs need fixing before relying on spatial acoustics in production.**

1. **[P1] All wrapper parameter names are empty.**
   The global `StringName` constants initialize before Godot’s string system. Runtime inspection confirmed all three graph inputs have `parameter_name=""`, so named spatial controls cannot resolve. Initialize these names after engine startup. [Location](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_graph_wrapper.cpp:4)

2. **[P1] GDScript cannot call the spatial parameter setter.**
   `set_parameter()` exists in C++ but isn’t bound. Runtime confirmed `playback.has_method("set_parameter") == false`; AudioManager therefore skips its entire spatial DSP update block. This needs fixing independently of issue 1. [Location](/Users/luong.pham/Work/godot/modules/symphony/stream/audio_stream_playback_symphony.cpp:33)

3. **[P1] Wrapped one-shots never finish.**
   A wrapped 0.1-second WAV still reports `is_playing=true` after mixing over one second. WavePlayer completion never stops the outer playback, preventing normal `finished`-based voice cleanup. Propagate source completion through the wrapper. [Location](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_graph_wrapper.cpp:125)

4. **[P1] Wrapping supported Godot audio formats can replace sound with silence.**
   The factory accepts arbitrary streams, but WavePlayer only supports 16-bit PCM WAV. I reproduced audible native 8-bit WAV playback becoming completely silent after wrapping; OGG/MP3 also cannot satisfy the required WAV cast. Decode supported formats or reject unsupported wrapping explicitly. [Factory](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_graph_wrapper.cpp:16), [format restriction](/Users/luong.pham/Work/godot/modules/symphony/nodes/generators/symphony_wave_player.h:184)

## Detailed findings: SpatialAcousticsEngine issues 5–10

### 5. Closed-door transmission compounds every frame — confirmed

The bug is in the interaction between the scheduled direct-path solve and the
always-per-frame portal pass:

1. `_solve_occlusion_for_emitter()` overwrites `target.transmission[]` with the
   direct-path result only when the scheduler services the emitter
   ([spatial_acoustics_engine.cpp:533-555](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:533)).
2. The portal pass runs for every active emitter whenever a graph exists
   ([spatial_acoustics_engine.cpp:220-237](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:220)).
3. On an unreachable path, the closed-portal fallback multiplies the current
   `target.transmission[]` by the door transmission
   ([spatial_acoustics_engine.cpp:487-531](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:487)).

Therefore, if the direct solve is not due on a frame, the already door-filtered
target is filtered again. The same happens indefinitely when occlusion is
disabled, because the portal pass still runs but no direct solve restores the
bands. The fallback also raises `target.occlusion` from the already-mutated
transmission. The reset of `portal_gain` and `apparent_position` at
[spatial_acoustics_engine.cpp:380-388](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:380)
does not reset the transmission bands.

The existing integration tests only assert that a closed portal has gain below
one and that an open portal has more gain than a closed one
([spatial_acoustics_integration_test.gd:331-375](/Users/luong.pham/Work/game-template/test/addons/symphony_audio/spatial_acoustics_integration_test.gd:331)).
They do not assert that a settled closed-door value is stable across additional
frames.

Recommended fix:

- Keep an unmodified per-emitter direct-solve base, e.g.
  `base_transmission[3]` and `base_occlusion`, in `EmitterState`.
- Write those fields from the direct occlusion result (and from any explicit
  direct-path setter) when a solve occurs.
- At the beginning of each portal composition, copy the base values into
  `target`, then apply the closed-portal multiplier once. Do not use the
  previous portal-composed `target` as the next frame’s base.
- Compose `occlusion` from `base_occlusion` and the newly composed transmission,
  rather than from the previous raised value. Keep
  `material_transmission[]` raw; it is already documented as the pre-volumetric,
  pre-portal debug value.
- Preserve the existing `air_cutoff_base` discipline: portal diffraction should
  continue to be assigned as `MIN(base, diffraction)` from a fresh base, not
  accumulated.

Add a live SceneTree regression that disables direct occlusion, closes a direct
portal, drives the same emitter for (for example) 60 more frames after it has
settled, and asserts that the gain remains within a small tolerance of its
pre-extension value. Add the same check with occlusion enabled to cover the
scheduled-solve path.

### 6. `membership_epoch` is never read; a room deletion leaves `last_src_node` stale — confirmed

The source-room cache stores both an ObjectID and a graph node index:

- The fast path looks up `last_src_room_id` through `ObjectDB`, checks
  `contains_point()`, and then trusts `last_src_node`
  ([spatial_acoustics_engine.cpp:390-411](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:390)).
- A cache miss performs the full `find_room_for_point()` scan and refreshes
  both fields.
- `_solve_room_for_emitter()` shares `last_src_room_id` and can set
  `last_src_node = -1` before the portal pass re-resolves it
  ([spatial_acoustics_engine.cpp:615-634](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:615)).
- A topology rebuild increments `membership_epoch`, rebuilds the graph, and
  remaps room ObjectIDs to the current registry-order node indices
  ([spatial_acoustics_engine.cpp:279-301](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:279)).

There is no per-emitter epoch field and no read of `membership_epoch` anywhere
in the engine. A valid cached room therefore passes `contains_point()` after an
unrelated room is removed, while its cached integer node can now refer to a
different room. Removing the source room itself is less problematic because
`ObjectDB::get_instance()` then returns null and forces a scan; removing a room
before the source room in registry order is the reproducible stale-index case.
The path cache is invalidated by the new graph epoch, but that does not repair
the stale `(src_node, lis_node)` input.

Room unregistering is otherwise correctly wired: `NOTIFICATION_EXIT_TREE` calls
`_unregister()`, the destructor calls it as a safety net, the room is removed
from the static vector, and `registry_epoch` is incremented
([acoustic_room_3d.cpp:12-52](/Users/luong.pham/Work/godot/modules/symphony/spatial/acoustic_room_3d.cpp:12)).
The graph sees that epoch on the next portal update. This means the defect is
not a dangling room pointer; it is trusting a node index across a registry
reorder.

Recommended fix:

- Add `uint64_t membership_epoch_seen` to `EmitterState`.
- At the start of the portal source-room resolution, compare it with the
  engine’s `membership_epoch`. On mismatch, clear both
  `last_src_room_id` and `last_src_node` before the fast path, then store the
  new epoch after resolution.
- Reset the same cache fields when registering or unregistering an emitter
  slot. `register_emitter()` currently resets target/smoothed values but does
  not reset `last_src_room_id` or `last_src_node`
  ([spatial_acoustics_engine.cpp:69-83](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:69)).
- As a defensive alternative, re-derive `last_src_node` from
  `_room_ptr_to_node` whenever the cached ObjectID is accepted; the epoch check
  is still preferable because it makes the invalidation contract explicit.

Add a live SceneTree test that creates an unrelated room before the source
room, establishes an open source-to-listener route, removes the unrelated room,
drives the engine again, and asserts that the apparent position remains at the
portal. Also assert the source-room deletion case falls back cleanly without a
stale ObjectID. The current C++ portal tests cover epoch increments and pure
membership math, but not engine cache invalidation
([test_symphony_portal.cpp:155-177](/Users/luong.pham/Work/godot/tests/modules/test_symphony_portal.cpp:155)).

This is directly at odds with the handoff’s claim that the membership cache is
“invalidated wholesale when the topology epoch bumps”
([HANDOFF_NOTES.md:215-230](/Users/luong.pham/Work/godot/modules/symphony/spatial/HANDOFF_NOTES.md:215)).
The graph epoch increments, but the emitter cache currently does not consume
that invalidation signal.

### 7. Multiple walls and `hit_from_inside` — confirmed backend mismatch

The template solver marches from source to listener, advances by
`ray_offset` after each hit, multiplies one material transmission per hit, and
then applies one unconditional square root whenever `hit_count > 1`
([occlusion_solver.h:90-169](/Users/luong.pham/Work/godot/modules/symphony/spatial/occlusion_solver.h:90)).
That correction is valid only when every solid barrier contributes exactly two
hits: entry and exit. It converts `t²` for one slab to `t`, and `t⁴` for two
slabs to `t²`.

The actual Godot Physics 3D backend uses `hit_from_inside = false` by default
in `PS3DT::RayParameters` ([physics_server_3d_types.h:40-50](/Users/luong.pham/Work/godot/servers/physics_3d/physics_server_3d_types.h:40)).
Its ray implementation explicitly ignores a shape when the next ray starts
inside it ([godot_space_3d.cpp:150-168](/Users/luong.pham/Work/godot/modules/godot_physics_3d/godot_space_3d.cpp:150)).
The occlusion wrapper never changes that flag
([occlusion_solver.cpp:38-63](/Users/luong.pham/Work/godot/modules/symphony/spatial/occlusion_solver.cpp:38)).
After the first hit enters a box, the next query can skip that box and hit the
next wall. Two physical walls can consequently produce two hits, and the
global square root turns `t²` into `t`, under-attenuating the path.

The current unit test does not expose this. Its `WallSet` deliberately returns
the exit face even when `from` is inside a wall, so two walls produce four hits
and the expected `t²` passes
([test_symphony_occlusion.cpp:20-61](/Users/luong.pham/Work/godot/tests/modules/test_symphony_occlusion.cpp:20),
[test_symphony_occlusion.cpp:126-140](/Users/luong.pham/Work/godot/tests/modules/test_symphony_occlusion.cpp:126)).
The live integration test uses one thin wall and does not distinguish one wall
from two ([spatial_acoustics_integration_test.gd:151-203](/Users/luong.pham/Work/game-template/test/addons/symphony_audio/spatial_acoustics_integration_test.gd:151)).

Do not fix this by simply setting `hit_from_inside = true`: a query that reports
a hit at the current interior origin can repeatedly report the same shape as
the march advances, and the result is backend-dependent. Recommended fix:

- Extend the injectable raycast result with a stable physical barrier key,
  preferably `(collider_id, shape)` from `PS3DT::RayResult`, in addition to the
  material.
- Remove the global `sqrt(accum)` inference.
- Apply each barrier’s material transmission once per distinct physical
  barrier/shape. If a supported shape can contain multiple independent
  barriers, the callback must provide a boundary/crossing classification or a
  dedicated multi-hit query; do not infer it from total hit count.
- Keep the synthetic tests for the four-hit entry/exit model, but add a
  Godot-style callback that ignores a shape when `from` is inside and asserts
  two distinct walls produce `t²`. Add a live PhysicsServer3D test with two
  boxes, and run it under both supported 3D physics backends if both are part
  of the supported matrix.

### 8. `apparent_position` is only seeded by the portal pass for pooled movement — confirmed

For an active pooled voice, `update()` refreshes `source_position` from the
voice pool at [spatial_acoustics_engine.cpp:148-159](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:148).
That path updates attenuation and max distance, but not
`target.apparent_position`. The ordinary setter does seed it
([spatial_acoustics_engine.cpp:721-727](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:721)),
which is why non-pooled tests can hide the problem.

The portal pass resets the apparent target to the true source, then optionally
redirects it ([spatial_acoustics_engine.cpp:380-388](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:380)).
However, that pass is skipped when portal propagation is disabled or when the
rebuilt graph has zero rooms
([spatial_acoustics_engine.cpp:220-237](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:220)).
In those cases a pooled source moved from one position to another keeps its old
smoothed apparent position. The public getter returns that stale smoothed value
([spatial_acoustics_engine.cpp:945-956](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:945)).

Recommended fix: after the pool/override position has been finalized, seed
`target.apparent_position = source_position` for every active emitter before
the optional portal stage. The portal stage can then override that baseline
only for a reachable route. If the public `set_emitter_apparent_position()`
override must remain supported for non-pooled callers, represent it as an
explicit override flag/value rather than allowing a stale target to act as an
implicit override.

Add a GdUnit4 test with no rooms and a real pooled 3D voice: move the pool slot
between two positions, call `SpatialAcousticsEngine.update()`, and assert the
apparent position follows the new position. Repeat with portal propagation
disabled. The existing open-portal test covers the redirect case, not this
baseline case ([spatial_acoustics_integration_test.gd:310-329](/Users/luong.pham/Work/game-template/test/addons/symphony_audio/spatial_acoustics_integration_test.gd:310)).

### 9. New emitters wait for the scheduler — confirmed

`register_emitter()` sets `first_update = true`, but `first_update` only affects
IIR smoothing in `_smooth_params()`; it is not passed to the scheduler
([spatial_acoustics_engine.cpp:69-83](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:69),
[spatial_acoustics_engine.cpp:768-775](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:768)).

For a fresh zero-initialized emitter, `last_update_time` starts at `0.0` and is
incremented each frame ([spatial_acoustics_engine.cpp:161-185](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:161)).
The scheduler only adds a candidate when the accumulated time reaches the
distance-scaled interval ([probe_scheduler.cpp:30-59](/Users/luong.pham/Work/godot/modules/symphony/spatial/probe_scheduler.cpp:30)).
At the default 10 Hz rate that is about 100 ms for a near emitter and up to
300 ms at the configured far multiplier. Until then, the default full-
transmission `SpatialParams` are published. A short one-shot can finish before
its first occlusion solve.

There is a second lifecycle issue: `register_emitter()` does not reset
`last_update_time`, `last_src_room_id`, or `last_src_node`. Reusing an emitter
array index can therefore inherit the previous voice’s timer and room cache.
That can make a recycled emitter solve too soon while a genuinely new index
waits.

Recommended fix:

- Add an explicit `force_initial_solve` bit to `EmitterState` and
  `ProbeScheduler::EmitterInfo`.
- Set it on registration and keep it set until the scheduled solver actually
  runs. A missing physics space must not consume the flag.
- Make the scheduler treat that bit as due, while still charging the normal
  estimated ray cost against the unified budget.
- Reset `last_update_time` and all per-emitter cache fields on registration;
  reset the timer after a solve as today
  ([spatial_acoustics_engine.cpp:190-213](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:190)).

An `INF` timer sentinel is a smaller alternative, but an explicit flag makes
the state visible, avoids timer overflow/semantics problems, and supports the
“do not consume until a real solve occurs” rule.

Add a scheduler unit test proving a new/forced emitter is selected even with
`last_update_time = 0`, plus an engine/integration test that registers a
blocked source, runs one update, and verifies the first available physics
frame produces occlusion. Add a recycled-slot test proving the timer and room
cache are reset. The current scheduler suite deliberately tests the opposite
case—an emitter with `last_update_time = 0.001` is not due
([test_symphony_probe_scheduler.cpp:151-166](/Users/luong.pham/Work/godot/tests/modules/test_symphony_probe_scheduler.cpp:151))—but has no initial-solve case.

### 10. Air absorption is incorrectly gated by the occlusion solve — confirmed

The distance calculation is currently at the end of
`_solve_occlusion_for_emitter()` ([spatial_acoustics_engine.cpp:597-611](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:597)).
That solver is only called from the scheduled loop when `occlusion_enabled` is
true ([spatial_acoustics_engine.cpp:190-213](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:190)).
The physics-space/scheduler gate itself is also conditional on occlusion or
room estimation being enabled ([spatial_acoustics_engine.cpp:110-124](/Users/luong.pham/Work/godot/modules/symphony/spatial/spatial_acoustics_engine.cpp:110)).

Consequences:

- `occlusion_enabled = false`, `room_estimation_enabled = false`: no scheduler
  runs, so air cutoff never updates.
- `occlusion_enabled = false`, `room_estimation_enabled = true`: room probes may
  run, but `_solve_room_for_emitter()` does not compute air absorption, so the
  cutoff still stays stale unless some other code sets it.
- With no valid physics space, distance-only air absorption also remains stale,
  even though it needs no physics ray.

The existing air test verifies the pure
`SpatialGraphWrapper::distance_to_air_cutoff()` function
([test_symphony_volumetric.cpp:156-171](/Users/luong.pham/Work/godot/tests/modules/test_symphony_volumetric.cpp:156)).
The engine no-ratchet test drives `set_emitter_air_cutoff()` directly and
therefore does not exercise this scheduling gate
([test_symphony_spatial_engine.cpp:112-139](/Users/luong.pham/Work/godot/tests/modules/test_symphony_spatial_engine.cpp:112)).
The live collider test has occlusion enabled, so it also cannot detect the
disabled-occlusion case ([spatial_acoustics_integration_test.gd:151-203](/Users/luong.pham/Work/game-template/test/addons/symphony_audio/spatial_acoustics_integration_test.gd:151)).

Recommended fix:

- Extract the distance-only calculation into
  `_update_air_absorption_for_emitter(int)`.
- Call it for every active emitter after the final source position is known and
  before portal composition, independently of physics, scheduler, room probes,
  and occlusion.
- When disabled, write both `air_cutoff_base` and the no-portal target to
  `20000 Hz`.
- Remove the duplicate write from `_solve_occlusion_for_emitter()`.
- Keep the portal pass’s fresh `MIN(air_cutoff_base, diffraction_cutoff)`
  composition so portal diffraction remains an additional, recoverable filter.

Add an engine test with occlusion and room estimation both disabled, no physics
space, and air absorption enabled: move a non-pooled emitter from near to far,
call `update()`, and assert `compute_air_cutoff()` decreases. Add a second test
with room estimation enabled but occlusion disabled to prove the room scheduler
does not become an accidental prerequisite.

## Existing coverage and handoff constraints

The handoff reports the completed baseline as **182 C++ cases / 195,815
assertions** and **14/14** cross-repo GdUnit4 cases. The relevant current
coverage is:

- Occlusion unit tests: clear path, thin plane, one slab, two synthetic slabs,
  total absorption, fallback material, hit cap, and beyond-listener handling.
  They do not test Godot’s start-inside query semantics.
- Scheduler unit tests: budget, correction, priority, cache-cost estimates,
  inaudible emitters, not-yet-due emitters, and fairness. They do not test a
  forced first solve.
- Engine unit tests: lifecycle, first-update smoothing, convergence,
  frame-rate-independent smoothing, manually driven air no-ratchet, volumetric
  blend arithmetic, and SeqLock integrity. They do not run the portal graph,
  pooled position pull, room deletion, or the scheduler/air gate together.
- Portal graph/router tests: Dijkstra routes, closed reachability, path-cache
  epoch invalidation, apparent-position math, diffraction, and gain. They do
  not exercise `SpatialAcousticsEngine`’s emitter membership cache.
- Live GdUnit4: real collider spectral/volumetric behavior, propagation delay,
  open/closed portal behavior, and apparent-position redirection. It does not
  cover closed-door stability, room deletion/reordering, pooled movement with
  no graph, forced initial occlusion, or air absorption with occlusion disabled.

The handoff explicitly says that headless C++ tests cannot safely exercise
`contains_point()` and live room/portal membership; those cases belong in the
live SceneTree integration layer
([HANDOFF_NOTES.md:345-360](/Users/luong.pham/Work/godot/modules/symphony/spatial/HANDOFF_NOTES.md:345)).
It also gives the relevant reset rule: any per-frame value folded into
`SpatialParams.target` must be reset before composition
([HANDOFF_NOTES.md:530-540](/Users/luong.pham/Work/godot/modules/symphony/spatial/HANDOFF_NOTES.md:530)).
Issue 5 violates that rule for closed-door transmission; issue 10 violates
the independent-solver assumption by placing air absorption inside the
occlusion solver.

The existing binary passed **182 tests / 195,815 assertions**, but these
additional regressions are not covered by that baseline. This review documents
the source-level findings and test recommendations; it does not apply the
runtime fixes.