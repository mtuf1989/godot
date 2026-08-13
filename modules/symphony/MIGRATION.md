# Symphony API Migration Notes (v1.0 real-time fix)

Track breaking changes introduced while implementing `improve_plan_1_7.md`.
Update this file with each commit that changes a public API.

## Status

- **Milestone:** M1 in progress (§1 baseline)
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
## Planned (from improve_plan_1_7.md)

- `ExtraArenaBytesFunc(params, mix_rate)` — rate-dependent sizing
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
