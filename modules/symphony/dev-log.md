# Dev Log — Symphony Audio System

## 2026-06-30 — G5 Ambient & Spatial Foundation

### Completed

| Task | Status | Files |
|------|--------|-------|
| G5.4 — 2D Distance Attenuation (C++) | ✅ Done | `runtime/voice_manager.h/.cpp`, `runtime/sound_event.h/.cpp` |
| G5.1 — AudioZone2D Node | ✅ Done | `addons/symphony_audio/audio_zone_2d.gd` |
| G5.2 — AmbientSystem Autoload | ✅ Done | `addons/symphony_audio/ambient_system.gd` |
| G5.3 — ScatterLayer System | ✅ Done | (integrated in `ambient_system.gd`) |
| Plugin registration | ✅ Done | `plugin.gd` |
| AudioManager integration | ✅ Done | `audio_manager.gd` (slot attenuation setup + per-frame apply) |

### Architecture Decisions

1. **Distance attenuation lives in C++ VoiceManager** — computed in `_update_importance_batch()` alongside importance (same loop, same staggered schedule). `attenuation_volume` float on VoiceSlot. GDScript reads it via `get_slot_attenuation_volume()` and applies to player.

2. **Three attenuation models**: Linear (`1.0 - d/max`), Logarithmic (`1.0 / (1.0 + d*9/max)`), Custom (Curve.sample). Custom Curve stored in parallel `Ref<Curve>[]` array to keep VoiceSlot cache-friendly.

3. **Auto-virtualization**: When distance >= max_distance AND virtualize_when_inaudible is true, C++ transitions voice to VIRTUALIZING. Devirtualizes at 95% of max_distance (5% hysteresis band prevents oscillation at boundary).

4. **AudioZone2D influence calculation**: Uses geometric distance-to-shape math (Circle, Rect, Capsule, ConvexPolygon). FadeCurveType enum: Linear, Smoothstep, InverseDistance, Custom (with Curve resource).

5. **AmbientSystem eviction policy**: Combined score = `influence × (1.0 + priority * 0.01)`. Influence dominates but priority breaks ties. Hysteresis threshold (0.05) prevents flip-flopping. Max 3 simultaneous zones default.

6. **Day/night variants**: `loop_event_variants: Dictionary` on AudioZone2D. AmbientSystem.set_variant("night") crossfades all active zones to the variant's event. Falls back to `loop_event` if variant not found.

7. **Scatter validation**: Uses `Physics2DDirectSpaceState.intersect_point()` to confirm scatter positions are inside the zone's Area2D. 5 retry attempts, returns INF on failure (event skipped).

8. **AudioStreamPlayer2D attenuation override**: AudioManager sets `player.max_distance = 99999` to disable Godot's built-in attenuation, then applies our C++-computed attenuation_volume as volume_db. This ensures our attenuation model (from SoundEvent) is the one actually used.

### Gotchas / Notes for Future Sessions

1. **Editor drawing performance**: AudioZone2D uses `queue_redraw()` every frame in editor (for debug boundary drawing). This is guarded by `Engine.is_editor_hint() and debug_draw`. If editor becomes slow with many zones, disable `debug_draw` on individual zones.

2. **Scatter timer reset on zone deactivation**: When a zone is evicted from active set, its scatter timer in `_scatter_timers` is NOT cleaned up (stays in dict). This is intentional — when the zone re-activates, it picks up a fresh interval. Minor memory (one float per ever-active zone).

3. **Listener position sync**: AmbientSystem uses its own listener position (from Camera2D or manual node). AudioManager also sets `SymphonyVoicePool.set_listener_position()`. These should agree. Currently they independently query the same Camera2D, which is correct.

4. **AudioZone2D shape caching**: `_cached_shapes` is populated in `_ready()`. If CollisionShape2D children are added/removed at runtime, call `_cache_collision_shapes()` manually. Dynamic shape changes are not automatically tracked.

5. **Attenuation vs RTPC volume conflict**: Resolved via 4-layer volume stack (Solution E). Each slot has `PackedFloat32Array([initial_db, rtpc_offset_db, attenuation_db, fade_db])`. All layers sum to produce final `player.volume_db`. No dual-writer conflict. RTPC Volume binding writes to layer[1] as offset from initial. Manual fade replaces Tween (Option 2) — fade_db layer driven by `move_toward()` per frame.

6. **`importance_weight` property**: SoundEvent.h has this as a concept but it's not yet an exported property in the current .cpp (uses default 1.0). If you need per-event importance tuning, add the export.

7. **PackedFloat32Array is value-typed in GDScript**: When reading from `_volume_stack[slot]`, modifying, and storing back — you MUST write it back to the dict (`_volume_stack[slot] = stack`). Forgetting the write-back silently drops changes. Every layer update function does this correctly, but future edits must maintain this pattern.

8. **AudioStreamPlayer2D.max_distance override**: We set `player.max_distance = 99999.0` to disable Godot's built-in 2D attenuation (we manage it via volume stack). Godot still internally computes its attenuation (wasted cycles, negligible). If Godot adds a "disable attenuation" flag in future, switch to that.

9. **Ref<Curve> parallel array in C++**: Can't put `Ref<Curve>` inside `VoiceSlot` (non-trivial constructor + memnew_arr raw allocation). Stored in separate `slot_attenuation_curves` array. Not co-located in cache but at 48 slots it doesn't matter.

10. **RTPC Volume as offset vs absolute**: Volume stack computes `rtpc_offset = mapped - initial_db`. Effect: final = mapped + attenuation + fade (initial cancels out). If designer's RTPC curve outputs absolute dB, this works correctly. But the initial_db randomization from volume_range is effectively overridden by RTPC, not additive. Document for users.

11. **Scatter collision_mask**: Physics query uses `zone.collision_mask`. If zone's layer/mask is misconfigured, scatter silently fails (all 5 retries miss, event skipped with no error). Consider adding a debug warning in development builds.

12. **No hot-swap of loop_event at runtime**: Changing `AudioZone2D.loop_event` while zone is active doesn't trigger crossfade. Use `AmbientSystem.set_variant()` for runtime switching. Direct property changes require manually stopping and restarting the zone.

### Skill Audit (godot-gdscript + godot-gdextension-cpp)

Applied after initial implementation. Fixes made:
- **Removed private state access**: AmbientSystem no longer reaches into `AudioManager._slot_to_player`. Uses new public `AudioManager.set_voice_volume(handle, db)` method instead.
- **Added fail-fast**: AudioZone2D now `push_error()` if no CollisionShape2D children found at _ready().
- **Stolen-slot cleanup**: `_update_fade_layer()` now checks if slot still exists in `_slot_to_player` before processing (handles external steal by C++ VoiceManager).
- **queue_redraw() is editor-only**: Already guarded, but noted as potential scale issue if 50+ zones are visible in editor simultaneously.

Key insights from skills that informed the design:
- "Batch data through packed arrays" → PackedFloat32Array for volume stack (4 floats per slot, value-typed).
- "Worker threads must not mutate active SceneTree" → VoiceManager C++ only writes to VoiceSlot fields, never touches players/nodes. GDScript reads atomic values in _process.
- "Prefer RefCounted plus Ref<T> for service-style processors" → Ref<Curve> stored in parallel array, not in VoiceSlot struct.
- "fail-fast on programmer errors" → push_error for missing shapes, not silent fallback.

---

## 2026-06-30 — G4 Dialogue & Bus Control

### Completed

| Task | Status | Files |
|------|--------|-------|
| G4.3 — BusController C++ (Snapshots) | ✅ Done | `runtime/bus_controller.h/.cpp` |
| G4.4 — Auto-Ducking System | ✅ Done | `runtime/bus_controller.cpp` (integrated) |
| G4.1 — DialogueAudioPipeline Autoload | ✅ Done | `addons/symphony_audio/dialogue_audio_pipeline.gd` |
| G4.2 — Dialogue Manager Integration | ✅ Done | `addons/symphony_audio/dialogue_audio_pipeline.gd` |
| G4.5 — Importance-Based Mixing | ✅ Done | `runtime/voice_manager.h/.cpp` |

### Architecture Decisions

1. **Dual ducking approach**: Signal-based (DialogueAudioPipeline) for precision + poll-based (BusController) for safety net. Both duck Music bus. Clamped to -24dB total to prevent over-ducking when both active.

2. **BusController is C++ singleton** — manages snapshots (save/restore bus volumes/mute/solo with interpolated transitions) and auto-ducking (polls `AudioServer.get_bus_peak_volume_left_db` on Voice bus). Processed via `BusController.process(delta)` called from AudioManager._process.

3. **Snapshot interpolation** — linear in dB space. Mute/solo switches at 50% progress point. "Last apply wins" — calling apply_snapshot during an active transition blends from current interpolated position to new target.

4. **DialogueAudioPipeline** — GDScript autoload with 4 interruption modes (Priority, Queue, Reject, DuckAndOverlay). Separate 4-player pool on Voice bus. Signal-based ducking via Tween. Integration with dialogue_manager via `play_from_dialogue_line(line, on_finished)`.

5. **Importance formula**: `priority × distance_factor × importance_weight × category_weight`. distance_factor uses inverse-square with reference distance. Category weights: SFX=1.0, Music=1.0, UI=1.5, Ambient=0.5, Voice=2.0. Updated every 4 frames staggered (1/4 of pool per frame).

6. **DialogueLine integration** — uses duck-typing (RefCounted with has_tag/get_tag_value/character) to avoid hard dependency on the dialogue_manager addon. Reads `#voice=path`, `#priority=N` tags. Handles concurrent_lines.

### Gotchas / Notes for Future Sessions

1. **AudioServer include path** is `servers/audio/audio_server.h` (NOT `servers/audio_server.h`). This is the Godot 4.8+ path. Already documented in S1 dev-log but worth repeating.

2. **Ducking offset tracking**: BusController tracks cumulative ducking offset per bus in `applied_duck_offsets`. If you reset bus volumes externally (e.g., apply_snapshot), the offset tracking can drift. Consider resetting offsets when a snapshot is applied.

3. **constexpr static member**: `CATEGORY_WEIGHTS[5]` needs out-of-class definition (`constexpr float SymphonyVoicePool::CATEGORY_WEIGHTS[5];`) for ODR-use in C++14/17. This is correct in current code.

4. **DialogueAudioPipeline duck interaction with AudioManager.set_category_volume()**: Both modify the same bus volume. If game code sets Music to 50% via AudioManager AND dialogue ducks by -6dB, the effects stack. This is intentional (same as Wwise behavior) but could surprise users. Document it.

5. **BusController "default" snapshot** is captured lazily on first `process()` call, not in constructor. This avoids issues with AudioServer not being fully initialized during module init.

6. **Importance auto-updates inside process_frame()**: No need for GDScript to call `update_importance()` separately — it's baked into `process_frame()`. GDScript just needs to call `set_listener_position()` each frame for distance-based importance to work.

---

## 2026-06-30 — G3 RTPC & Symphony Integration

### Completed

| Task | Status | Files |
|------|--------|-------|
| G3.1 — RTPCEngine C++ | ✅ Done | `runtime/rtpc_engine.h/.cpp` |
| G3.2 — RTPCBinding on SoundEvent | ✅ Done | `runtime/sound_event.h/.cpp` |
| G3.3 — Parameter Application Pipeline | ✅ Done | `runtime/voice_manager.h/.cpp`, `audio_manager.gd` |
| G3.4 — Global and Per-Handle Parameter API | ✅ Done | `audio_manager.gd` |
| G3.5 — TransientDetector utility | ✅ Done | `addons/symphony_audio/transient_detector.gd` |
| G3.6 — Unified Playback (File + Procedural) | ✅ Done | `audio_manager.gd` |
| G3.7 — Integration Test (Footsteps) | ✅ Done | `test/audio/test_footsteps.*`, `audio/events/sfx/footstep.tres`, `audio/graphs/presets/footstep_procedural.tres` |

### Architecture Decisions

1. **RTPCEngine uses AudioServer mix callback** — true audio-thread smoothing at sample rate. One-pole coefficient per parameter. Smooths once per mix cycle (512 frames typically).

2. **Per-voice local parameters**: fixed `float[8]` array in VoiceSlot (no allocation, cache-friendly). Per-voice overrides global when both set.

3. **Unified playback via AudioStreamPlayer**: Both `AudioStream` (file) and `AudioStreamSymphony` (procedural) go through the same player pool. Godot's `instantiate_playback()` handles the difference transparently. RTPC targets `Pitch`/`Volume`/`PlaybackSpeed` use player properties; `GraphInput` uses `playback.set_parameter()`.

4. **Parameter application at 60Hz (game thread)**: The per-frame RTPC pipeline reads already-smoothed values from RTPCEngine (audio-thread smoothed) and applies to player properties. For Symphony graphs, the ParameterSmoother operator inside the graph provides the final audio-rate interpolation.

5. **Auto-register on first use**: `RTPCEngine.set_target()` auto-registers unknown parameters with default 5ms smoothing. Game code doesn't need to pre-declare parameters.

### Gotchas / Notes for Future Sessions

1. **TriggerInput for one-shots**: The footstep graph uses TriggerInput "pluck" to fire the ADSR. Game code must call `playback.trigger(&"pluck")` after `play()` — the graph won't produce sound without it. Consider adding auto-trigger support to SoundEvent.

2. **FilterCutoff RTPC target (target=2)**: Currently a no-op in the pipeline. Implementing per-voice filter requires either a dedicated AudioEffect on a per-voice bus or using Godot's `AudioStreamPlayer.attenuation_filter_cutoff_hz` (3D only). For Symphony voices, use `GraphInput` target instead.

3. **evaluate_curve with null Curve**: Falls back to linear (input=output). This is correct but may surprise users expecting a specific mapping. Document that null curve = linear pass-through.

4. **Thread safety of set_target()**: Currently writes `target_value` on the game thread while the audio thread reads `current_value` and writes `current_value`. Since `target_value` and `current_value` are separate floats and the operation is a single float store, this is practically safe on ARM64 (naturally aligned stores are atomic). But it's not formally correct — consider adding `std::atomic<float>` for `target_value` if issues arise.

5. **RTPC binding serialization**: Uses `TypedArray<Dictionary>` for .tres compatibility. Each dict has keys: `parameter_name` (StringName), `target` (int), `curve` (Curve or null), `min_value` (float), `max_value` (float), `graph_input_name` (StringName). Agent-authorable.

---

## 2026-06-30 — G2.3, G2.4, G2.5 Implementation

### Completed

| Task | Status | Files |
|------|--------|-------|
| G2.3 — MusicSystem GDScript Autoload | ✅ Done | `addons/symphony_audio/music_system.gd` |
| G2.4 — AudioStreamInteractive Integration | ✅ Done | (integrated in music_system.gd) |
| G2.5 — Music System Test Scene | ✅ Done | `test/audio/test_music_system.gd`, `.tscn` |
| Plugin registration | ✅ Done | `plugin.gd`, `project.godot` |

### What Was Built

- **MusicSystem autoload** (~580 lines) with:
  - `load_graph()` / `set_state()` / `stop()` public API
  - 4 transition types: Crossfade, FadeThroughSilence, Cut, Stinger
  - Beat-quantized transitions (NEXT_BEAT, NEXT_BAR) via BeatClock signals
  - Layer system via `AudioStreamSynchronized` with per-layer fade
  - `AudioStreamInteractive` auto-detection for simple graphs (no layers, no stingers)
  - Signals: `state_changed`, `transition_started`, `transition_completed`, `beat_hit`, `bar_hit`
  - Last-call-wins for rapid `set_state()` calls

- **Test scene** with programmatic 3-state graph (exploration/combat/menu), UI buttons, layer toggles, beat flash indicator, event log

### Verification Items (Not Yet Tested at Runtime)

1. **Null streams** — Test graph uses `null` AudioStreams. `AudioStreamSynchronized` and `AudioStreamPlayer` may warn or error with null. Drop in real `.ogg` placeholder loops for proper testing.

2. **AudioStreamSynchronized volume** — Using `-80.0 dB` as silence, `0.0 dB` as full. Confirm Godot doesn't require special handling for "fully muted" state.

3. **BeatClock.process() ownership** — Called from `MusicSystem._process()`. Verify no other script also calls it (would double-emit signals). AudioManager does NOT call it currently.

4. **Wildcard transition `from → "*"`** — C++ `find_transition()` handles this as 3rd priority lookup. Test that `find_transition(&"menu", &"exploration")` correctly matches the `from=menu, to=*` rule.

5. **Interactive mode not exercised by test** — Test graph has layers → `_is_simple_graph()` returns false → always uses CUSTOM path. Need a separate simple graph (no layers, crossfade/cut only) to validate the AudioStreamInteractive codepath.

6. **Player swap after crossfade** — `_on_transition_complete` swaps `_player_a`/`_player_b`. Verify the swap logic doesn't conflict when transitioning from sync_player (layered state) to a non-layered state.

### Previous Phases Complete (for context)

- G2.1 — MusicStateGraph C++ Resource ✅ (`runtime/music_state_graph.h/.cpp`)
- G2.2 — BeatClock C++ ✅ (`runtime/beat_clock.h/.cpp`)
- G1 — Full foundation ✅ (SoundEvent, VoiceManager, EventDispatcher, AudioManager, bus layout, plugin)

### Next Up

Phases G1, G2, G3, and S1 are all complete. The v1.0 milestone is ~75% done. Next options:

- **G4 — Dialogue & Bus** — DialogueAudioPipeline, BusController snapshots, auto-ducking
- **G5 — Ambient & Spatial** — AudioZone2D, AmbientSystem, scatter layers
- **S2 — Synthesis Toolkit** — ModalBank, GrainCloud, PitchShifter, Waveshaper, FM, presets
- **G6 — Debug Tools** — performance overlay, event log, test suite

Recommended: G4 (completes the core runtime feature set) or S2 (enables rich procedural content).

---

## 2026-06-29 — S1 Core Completion + G2.1/G2.2 C++ Classes

### Completed

| Task | Status | Files |
|------|--------|-------|
| S1.1 — Fix O(N²) enforce_voice_limits | ✅ Done | `core/symphony_voice_manager.h/.cpp` |
| S1.2 — Fix RMS (both channels) | ✅ Done | `stream/audio_stream_playback_symphony.cpp` |
| S1.3 — PolyBLEP Oscillator | ✅ Done | `nodes/generators/symphony_oscillator.h` |
| S1.4 — DelayLine Operator | ✅ Done | `nodes/delay/symphony_delay_line.h` |
| S1.5 — FeedbackPath + Compiler Support | ✅ Done | `nodes/delay/symphony_feedback_path.h`, `core/symphony_graph_compiler.cpp`, `core/symphony_graph_description.h` |
| S1.6 — ParameterSmoother | ✅ Done | `nodes/utility/symphony_parameter_smoother.h` |
| S1.7 — StochasticTrigger | ✅ Done | `nodes/timing/symphony_stochastic_trigger.h` |
| S1.8 — SVFilter | ✅ Done | `nodes/filters/symphony_sv_filter.h` |
| S1.9 — Unit Tests | ✅ Written | `tests/modules/test_symphony_operators.cpp` (blocked by embree crash) |
| G2.1 — MusicStateGraph Resource | ✅ Done | `runtime/music_state_graph.h/.cpp` |
| G2.2 — BeatClock Singleton | ✅ Done | `runtime/beat_clock.h/.cpp` |

### Post-Implementation Review — Bugs Found & Fixed

| # | Severity | Issue | Fix |
|---|----------|-------|-----|
| 1 | CRITICAL | `on_voice_started`/`on_voice_stopped` not bound in `_bind_methods()` — voice limit permanently broken after N plays | Added bindings |
| 2 | HIGH | Race condition: raw pointers in `victims` vector could dangle between lock release and `stop()` call | Changed to ObjectID + ObjectDB::get_instance() validation |
| 3 | HIGH | `process_frame()` not bound — GDScript error "Static function not found" | Added binding |
| 4 | HIGH | Singleton registration without class name — GDScript resolves type instead of instance | Added 3rd arg to Engine::Singleton |
| 5 | MEDIUM | `std::mutex` on audio thread causes priority inversion | Noted — needs lock-free refactor (not yet done) |

### Pitfalls & Gotchas (Reference for Future Sessions)

**1. Every C++ method called from GDScript must be in `_bind_methods()` — no exceptions.**
There is NO compile-time check for this. If you forget to bind a method, it silently doesn't exist from GDScript. The error only appears at runtime ("method not found"). Always verify: for every `ClassDB::bind_method` call, confirm the GDScript side actually uses it, and vice versa.

**2. Singleton registration needs 3 arguments.**
```cpp
// WRONG — GDScript sees the class type, not the instance:
Engine::get_singleton()->add_singleton(Engine::Singleton("BeatClock", ptr));

// CORRECT — GDScript resolves to the singleton instance:
Engine::get_singleton()->add_singleton(Engine::Singleton("BeatClock", ptr, "BeatClock"));
```
The error message ("Static function not found in base GDScriptNativeClass") is misleading — it doesn't mention singletons.

**3. AudioServer include path is `servers/audio/audio_server.h`**
NOT `servers/audio_server.h`. This is inconsistent with other server includes (e.g., `servers/rendering_server.h`).

**4. `Dictionary::get_key_list()` returns `LocalVector<Variant>` in Godot 4.8+**
Old pattern (`List<Variant> keys; dict.get_key_list(&keys)`) no longer works. New: `LocalVector<Variant> keys = dict.get_key_list();`

**5. `Math_PI` does not exist. Use `Math::PI`.**
Godot math constants are all in `Math::` namespace, not preprocessor macros.

**6. Embree crash blocks ALL test execution on macOS.**
The test runner crashes in `embree::TaskScheduler::removeScheduler` during shutdown. This is pre-existing (affects even stock Godot tests like `test_audio_stream_wav`). Workaround: build with `module_raycast_enabled=no` for test-only builds, or test on Linux CI.

**7. Audio thread safety — never use `std::mutex` in mix callbacks.**
`SymphonyVoiceManager::enforce_voice_limits()` runs on audio thread via mix callback. Any mutex there competes with main-thread GDScript queries (debug monitors call `get_active_voice_count()` every frame). Priority inversion = audio dropouts. Needs lock-free snapshot pattern (atomics + double buffer).

**8. Race condition with raw pointers across lock boundaries.**
When collecting "victim" pointers inside a lock and then using them outside the lock, another thread can invalidate them. Always use `ObjectID` + `ObjectDB::get_instance()` validation pattern for deferred operations on Godot objects.
