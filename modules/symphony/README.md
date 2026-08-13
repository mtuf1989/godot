# Symphony

In-tree **Godot C++ module** (`modules/symphony/`). It is **not** a GDExtension: you compile it with the engine.

This folder lives inside a Godot source tree. Build from the **Godot repo root**, not from this directory.

## Prerequisites (macOS)

- Xcode Command Line Tools
- Python 3 + [SCons](https://scons.org/) (`pip3 install scons`)
- Vulkan SDK / MoltenVK if you need the Vulkan renderer (optional for headless tests)

Confirm you are at the engine root (`SConstruct` present) and this module is at `modules/symphony/`.

## Enable / disable

Godot compiles every module under `modules/` by default. To skip Symphony:

```bash
scons module_symphony_enabled=no
```

There is no separate “build the .dylib” step. Symphony is linked into `bin/godot.*`.

## Editor (day-to-day)

From the Godot repo root:

```bash
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
```

| Flag | Why |
|------|-----|
| `platform=macos` `arch=arm64` | Apple Silicon editor. Use `arch=x86_64` on Intel. |
| `target=editor` | Editor + tests. Game exports use `template_release` / `template_debug`. |
| `tests=yes` | Builds doctest cases under `tests/modules/test_symphony_*.cpp`. |
| `module_raycast_enabled=no` | Faster local compiles; Symphony does not need raycast. |
| `-j$(sysctl -n hw.ncpu)` | Parallel compile. On Linux use `-j$(nproc)`. |

Binary: `bin/godot.macos.editor.arm64`

### Run Symphony tests

```bash
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'
```

Equivalent filter:

```bash
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'
```

**Do not** pass `'[Symphony]'` alone. Doctest treats `[...]` as a character-class glob and will run almost every engine test.

After a small C++ change, the same `scons` line is incremental (only dirty TUs + relink).

## Release mix-timing gates

Strict median/p99 gates run only on `template_release` (not the editor):

```bash
scons platform=macos target=template_release arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.template_release.arm64 --headless --test --test-case='*Mix timing*'
```

Baselines are macos-arm64 specific and can flake under heavy host load.

## ThreadSanitizer (dev only)

TSan is **not** shipped in games. It produces a separate, slower binary with a `.san` suffix:

```bash
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=1 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --source-file='*symphony*'
```

First TSan compile is a full engine rebuild. GrainCloud µs/unit and release mix-timing gates are skipped or unenforced under sanitizers (instrumentation distorts timings).

## Other platforms (same module)

```bash
# Linux
scons platform=linuxbsd target=editor tests=yes module_raycast_enabled=no -j$(nproc)

# Windows (from an MSVC/SCons environment)
scons platform=windows target=editor tests=yes module_raycast_enabled=no
```

Adjust `arch` if needed. Tests and binary names follow Godot’s usual `bin/godot.<platform>.<target>.<arch>` pattern.

## Layout

```
modules/symphony/
├── core/       Graph compiler, arena, packages, VoiceManager, RT-scope
├── nodes/      DSP operators
├── runtime/    EventDispatcher, RTPCEngine, BeatClock, BusController, …
├── stream/     AudioStreamSymphony + playback
├── editor/     Graph editor (editor target only)
└── thirdparty/pffft/   FFT used by spectral nodes
```

## Notes

- Expected `ERROR:` from a stress test that sets `global_limit_bytes=1` (budget rejection). That case is intentional.
- `GrainCloud` may warn about unused `trigger_value` while compiling `register_types.cpp` (pre-existing).
- API breaks for GDScript are listed in `MIGRATION.md`. Authoring/runtime usage is in `user_guide.md`.

## Using it in a game

1. Build this custom Godot (editor and export templates) with Symphony enabled.
2. Point the game project (`game-template/`, separate repo) at that editor.
3. Symphony classes (`AudioStreamSymphony`, `RTPCEngine`, …) are engine singletons/types — there is no `.gdextension` plugin to copy.
