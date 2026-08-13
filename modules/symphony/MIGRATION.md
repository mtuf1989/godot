# Symphony API Migration Notes (v1.0 real-time fix)

Track breaking changes introduced while implementing `improve_plan_1_7.md`.
Update this file with each commit that changes a public API.

## Status

- **Milestone:** M1 — pause for review (core compiler/DSP/silence landed)
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
- Compiler reserves arena bytes before allocation; `CompiledGraph::destroy`
  releases them. Oversized graphs fail with a compile error (approach A).

### Silence behavior + DSP (§7/§8 partial)

- Operators use `SilenceBehavior::{ALWAYS_PROCESS,STATEFUL_TAIL,STATELESS}`.
- Shared `SymphonyFastMath::fast_sine` (quarter-wave folding).
- ADSR release uses note-off envelope value; SVFilter is TPT/ZDF; PitchShifter
  dry-bypasses at ~0 semitones; FDN caches controls and uses `exp2` for RT60 gains.

## Planned (from improve_plan_1_7.md)

- Charge non-arena + SharedPCM into the same reservation path
- Package counts (active/pending/outgoing/retired) wired to playback
- FDN fractional-tap smoothing (20 ms) without permanent dual-tap
- Full rate×micro-block matrix tests (22.05–96 kHz × 32/64)
- `trigger(name, value)` returns `bool`; dropped-trigger metrics
- RTPCEngine registration returns stable handles; no audio-thread auto-create
- `acquire_slot()` free-only; EventDispatcher owns stealing; `set_slot_rms()`
- Remove required `process_deferred_lod()` from GDScript (main-thread drain)
- LOD graph mutation/query APIs and `lod/<tier>/...` serialization
- Connection `is_feedback` serialization
- Memory / transition / retirement metrics (read-only)

## Game Audio Layer (`game-template/`)

Pending until corresponding C++ APIs land. Expected touch points:

- `AudioManager` / `process_deferred_lod()` call sites
- Event play paths that assume steal-inside-`acquire_slot`
- RTPC registration if analysis auto-create is removed
