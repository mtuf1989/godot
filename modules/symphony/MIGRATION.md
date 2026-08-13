# Symphony API Migration Notes (v1.0 real-time fix)

Track breaking changes introduced while implementing `improve_plan_1_7.md`.
Update this file with each commit that changes a public API.

## Status

- **Milestone:** M3 — RT-scope landed; TSan stress test added (run `use_tsan=yes` to gate)
- **Branch:** `features/symphony_fixed`

## Completed

### Test harness (§1)

- Operator unit tests must supply input/output pointer arrays sized to the
  operator descriptor pin counts (e.g. Oscillator: 2 inputs, 1 output).
- Symphony doctest filter tags:
  - `[Symphony][Operators]`
  - `[Symphony][Compiler]`
  - `[Symphony][Playback]`
  - `[Symphony][Spectral]`
  - `[Symphony][Voice]`
  - `[Symphony][Serialization]`
- Run Symphony tests with an explicit doctest filter (shell-safe):

```bash
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'
# or:
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'
```

  Note: passing `'[Symphony]'` alone is treated as a character-class glob by
  doctest and matches nearly all engine tests.

### RTPCEngine construction without AudioServer

- `RTPCEngine` no longer assumes `AudioServer` exists at construction.
  Unit-test setup does not create `AudioServer` except for `[Audio]` cases.
  Mix callback registration is skipped when the server is null; default
  sample rate falls back to 48 kHz until audio is available.

### ExtraArenaBytesFunc is rate-aware

- Signature is now `ExtraArenaBytesFunc(params, mix_rate)`.
- DelayLine, PitchShifter, FDNReverb, and GrainCloud use shared
  `resolve_config(params, mix_rate)` for estimation and construction.
- Graph compiler Phase 5 uses checked alignment-aware planning (no 25% headroom).
- `CompileResult` exposes `arena_bytes`, `arena_used_bytes`, `non_arena_bytes`,
  `trigger_buffer_bytes`, `route_metadata_bytes`, and `total_package_bytes`.

### SymphonyMemoryBudget

- Main-thread budget service with compile-time platform defaults:
  - Per graph: 8 MiB
  - Global: 128 MiB desktop / 64 MiB mobile / 32 MiB web
- Compiler reserves **total package bytes** (arena + non-arena metadata) before
  allocation; `CompiledGraph::destroy` releases them. Oversized graphs fail
  with a compile error (approach A).
- Global headroom includes unique SharedPCM cache bytes (`try_reserve_shared` /
  `release_shared`); SharedPCM is counted once per unique key, not per voice.
- Compile drains `GraphPackageRetirement` before reserving.

### LOD / feedback authoring (§11)

- Prefix-aware serializer for `graph/...` and `lod/<tier>/...` (tiers 1–2).
- Connection property `is_feedback` (defaults false for older resources).
- APIs: `add_lod_variant`, `duplicate_main_to_lod`, `set_lod_variant`,
  `remove_lod_variant`, `get_lod_variant`, `has_lod_variant`,
  `estimate_tier_memory`, `validate_tier_compile`.
- Editor: LOD tier selector, Dup→LOD, FB Toggle (UndoRedo), per-tier memory label.

### Silence behavior + DSP (§7/§8 partial)

- Operators use `SilenceBehavior::{ALWAYS_PROCESS,STATEFUL_TAIL,STATELESS}`.
- Shared `SymphonyFastMath::fast_sine` (quarter-wave folding).
- ADSR release uses note-off envelope value; SVFilter is TPT/ZDF; PitchShifter
  dry-bypasses at ~0 semitones; FDN caches controls and uses `exp2` for RT60 gains.

### PreparedGraphPackage (§4 partial)

- Playback publishes `PreparedGraphPackage*` (compiled graph + GraphOutput +
  sorted param/trigger routes) through one atomic `pending_package` slot.
- Audio `mix()` adopts packages without `rebuild_routing_tables()`, HashMap
  mutation, or `dynamic_cast`.
- `set_parameter` / `trigger` binary-search sorted routes on the current package.
- `swap_graph(CompiledGraph*)` still wraps into a package (call sites unchanged).
- Packages carry per-operator fingerprints (`node_id` + type hash + structural
  hash of exportable state size). Audio adopts pending packages and migrates
  ≤256-byte compatible state at the block boundary; large histories are skipped.
- Main-thread `swap_graph` no longer torn-reads live operator state.
- Main-thread `set_parameter` / `trigger` load an atomic `control_package`
  (published by audio on install), resolve routes, and retry once if a swap
  races the call — they never read the audio-only `current_package` pointer.

### GraphPackageRetirement (§6 partial)

- Audio thread pushes superseded / stopped packages onto a lock-free intrusive
  stack (`GraphPackageRetirement::retire`).
- `AudioServer` update callback drains and destroys packages on the main thread.
- Never-adopted pending packages displaced on the main thread are destroyed
  immediately. Playback destructor also drains any leftover retirement entries.

### Budget-aware transitions (§5 partial)

- Admitted swaps/LOD use a 40 ms equal-power crossfade (`SymphonyFastMath`).
- Crossfade tokens: 2 desktop / 1 mobile+web (`SymphonyVoiceManager`).
- Admission denied (no token, at/above warning CPU, or already transitioning)
  uses a 64-sample single-graph fade-out → swap → fade-in fallback.
- At most current + outgoing (+ held incoming during fallback).
- Transition outcomes are counted (`crossfade_transition_count` /
  `fallback_transition_count`) and included in `get_debug_metrics()`.

### Read-only debug metrics

- `SymphonyVoiceManager.get_debug_metrics()` returns memory snapshot fields,
  package lifecycle counts, dropped triggers, spectral underflows (PhaseVocoder
  incomplete-window skips), retirement pending/peak/destroyed, and transition
  counters. Individual getters mirror the same atomics.

### Stress / mix timing

- `tests/modules/test_symphony_stress.cpp` covers global budget rejection without
  reservation leaks, failed compiles leaving the audible package intact, peak
  live package caps, retirement teardown, and 10/30/50-node median/p99 mix
  timings.
- Editor/debug builds keep a soft absolute ceiling (`p99 < 5000 µs`).
- `template_release` builds also enforce stored macos-arm64 baselines. Each graph
  runs 3 trials of `64 × (32 × 512 frames)` @ 48 kHz; the gated median/p99 are the
  median across trials. Hard fail is strict plan acceptance: median ≤ +5% (min +2 µs),
  p99 ≤ +10% (min +5 µs). Expect flake under heavy host load; recalibrate constants if
  the reference machine or graph builders change.

### Spectral + cost calibration

- PhaseVocoder/SpectralGate `extra_cost_fn` adds `N·log2(N)·hops` per micro-block
  (SpectralGate ×0.7). PFFFT includes use the module path so tests can compile
  spectral headers.
- Spectral tests cover stretch=1 unity (±0.5 dB), stretch=2 finite output,
  underflow counter monotonicity, cleanup idempotence, and FFT-size cost scaling.
- SpectralGate applies the same COLA `1/Σw²` table as PhaseVocoder; open-threshold
  (threshold=-96) unity is gated to ±0.5 dB. `threshold_db` remains clamped to
  ≤0 dB (create + execute).
- GrainCloud `extra_cost_fn` adds surplus concurrent-grain and pitch-tracking cost
  beyond the base `cost_per_sample=24`. Stress test compares µs/cost_unit against an
  oscillator reference (must stay ≤2×).

### VoiceManager deferral (§6 partial)

- Audio `enforce_voice_limits()` only snapshots + writes per-voice atomics
  (`request_lod_tier` / `request_manager_stop`). No ObjectDB, `Vector`, or `stop()`.
- Priority / max LOD are cached on the playback at `start()` for audio reads.
- Main-thread drain runs via `AudioServer` update callback (and still via
  `process_deferred_lod()` for compatibility). At most one LOD compile per update.
- GDScript no longer needs to call `process_deferred_lod()` every frame.

### Triggers (§9 partial)

- `TriggerBuffer` capacity equals `SYMPHONY_MICRO_BLOCK_SIZE` (32/64); `push` returns bool.
- `TriggerInput` uses a fixed 64-entry SPSC queue; `fire()` / playback `trigger()` return `bool`.
- Dropped triggers increment a process-wide counter (`SymphonyVoiceManager.get_dropped_trigger_count()`).

### RTPCEngine handles (§9 partial)

- `register_global_parameter` / `register_analysis` return a stable `int` handle.
- `set_parameter_target` / `set_analysis` return `bool` and **do not** auto-register.
- Missing handles increment `get_missing_handle_count()`.
- Audio smoothing uses precomputed `block_alpha` (multiply/add only; recompute on rate/frame-size change).

### Voice steal accounting (§10 partial)

- `SymphonyVoicePool.acquire_slot()` is free-only (returns `-1` when full).
- `EventDispatcher` steals: same-event at cap (steal_mode), else global among
  `priority <= incoming`. Modes: oldest / quietest / farthest (+ importance, slot index).
- `reclaim_slot()` + `on_voice_stopped`/`on_voice_started` keep event counts correct.
- `set_slot_rms()` / `rms_valid`; quietest falls back to importance when RMS unknown.
- Play result `RESULT_STOLEN` with reason `oldest`/`quietest`/`farthest`.

### PhaseVocoder (§9 partial)

- Absolute `uint64_t` input/analysis positions; skip hops with incomplete windows.
- Precomputed COLA gain table (`1/Σ w²`) applied at synthesis.
- PFFFT setup destroyed on arena allocation failure (PhaseVocoder + SpectralGate).

### Package count metrics

- `SymphonyMemoryBudget` tracks active/pending/outgoing via atomics; retired count
  mirrors `GraphPackageRetirement::get_pending_count()` in snapshots.

### Real-time scope guards (§6)

- `SymphonyRealtimeScope` is a thread-local depth around playback `mix()`,
  `CompiledGraph::execute()`, `SymphonyVoiceManager` mix callback, and
  `RTPCEngine` mix callback.
- Symphony alloc / free / mutex / ObjectDB / compile / dynamic-container sites
  call `symphony_rt_note()`. Hits increment process-wide counters and `DEV_ASSERT`
  in dev builds. Tests use `SymphonyRealtimeAssertSuppressor` to observe counts.
- `GraphPackageRetirement::drain()` is flagged as `Free` if called inside an
  audio scope (drain stays on the AudioServer main-thread update callback).
- `SymphonyVoiceManager.get_rt_violation_count()` and debug metric `rt_violations`
  expose the process-wide counter (should stay 0 in production).
- Concurrent stress: `tests/modules/test_symphony_stress.cpp` mixes on a worker
  thread while the main thread swaps, sets parameters, fires triggers, drains,
  and stops. Run the Symphony suite under ThreadSanitizer:

```bash
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'
```

## Planned (from improve_plan_1_7.md)

- Charge non-arena + SharedPCM into the same reservation path
- Calibrated incoming transition cost units in admission
- LOD graph mutation/query APIs and `lod/<tier>/...` serialization
- Connection `is_feedback` serialization
- Memory / transition / retirement metrics (read-only) — partial (package counts done)

## Game Audio Layer (`game-template/`)

Pending until corresponding C++ APIs land. Expected touch points:

- `AudioManager` / `process_deferred_lod()` call sites (optional now; safe to remove)
- Event play paths: `acquire_slot` no longer steals; handle `RESULT_STOLEN`
- RTPC: must `register_global_parameter` / `register_analysis` before set; handle return values
