# Symphony Handoff Notes — Next Session

**Date:** 2026-08-13  
**Branch:** `features/symphony_fixed`  
**Plan source of truth:** `modules/symphony/improve_plan_1_7.md` (§6 gate + Verification)  
**Do not edit:** `modules/symphony/review_version_1_7.md`  
**Migration log:** `modules/symphony/MIGRATION.md`  
**User guide:** `modules/symphony/user_guide.md` (v1.7.1 changelog started)

---

## Where We Are

| Milestone | Status |
|-----------|--------|
| **M1** | Done |
| **M2** | Closed |
| **M3** | Nearly complete — RT-scope + TSan suite green; **next: M3 wrap-up docs** |

**HEAD (newest first):**

- `f6850bac04` — Thread-local `SymphonyRealtimeScope` + RT-guard tests + concurrent mix stress
- `c9ed415b3d` — GrainCloud density/pitch `extra_cost_fn` + µs/unit ≤2× stress guard
- `0d06281342` — SpectralGate COLA + open-threshold unity ±0.5 dB
- `c657ad4b45` — Strict release mix-timing baselines (+5% / +10%)
- `e9b517d919` — Handoff notes for M3 close-out
- `ff4c2235ce` — Spectral suite depth + FFT `extra_cost_fn`
- `52cd2e8aaa` — Memory stress + 10/30/50-node mix timing
- `93ff25a112` — Read-only transition / trigger / spectral / retirement metrics
- `5842cf7fce` — Atomic `control_package` for `set_parameter` / `trigger`

**Tests (last run):** `55/55` Symphony cases pass (editor). TSan `.san` binary: no data-race reports; GrainCloud µs/unit gate skipped under sanitizers.

```bash
# Editor
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'

# Release mix-timing gates
scons platform=macos target=template_release arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.template_release.arm64 --headless --test --test-case='*Mix timing*'

# TSan (binary suffix .san)
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=1 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --source-file='*symphony*'
# Prefer --test-case='*Symphony*' over '[Symphony]' (doctest char-class trap)
```

**Release mix timing baselines** (3-trial median of `64 × (32×512)` frames @ 48 kHz, macos arm64):

| Graph | baseline median | baseline p99 |
|-------|----------------:|-------------:|
| 10-node | 209 µs | 335 µs |
| 30-node | 1254 µs | 1747 µs |
| 50-node | 2420 µs | 2790 µs |

Per-512f equivalent ≈ median/32 (~6.5 / 39 / 76 µs). Strict gates: median ≤+5%, p99 ≤+10%.

---

## Locked Decisions (do not reopen unless asked)

- Stop at each milestone for review; follow plan order
- One branch, multiple focused commits; breaking APIs OK
- CPU: warning 70% / critical 90%; memory 8 MiB/graph, global 128/64/32 (desktop/mobile/web)
- Budget rejection: compile error + metrics only (approach A)
- Transitions: 40 ms equal-power when admitted; else 64-sample single-graph fallback
- Crossfade tokens: 2 desktop / 1 mobile+web
- Keep auto-LOD; silence −120 dB + 2-block hysteresis; no large-history migration
- Tests “green enough to iterate”
- **TSan + RT-scope unlocked** — kick off next session (was deferred; now M3 close-out)
- State migrate at **audio boundary** (not review mailbox); match node ID + type + structural hash; ≤256 bytes
- **game-template is out of scope**; API migrations documented in `MIGRATION.md` + user_guide changelog

---

## Landed and Reliable (do not re-litigate)

### Earlier M3
- Exact package memory charging; SharedPCM unique charging
- Calibrated crossfade admission (EWMA µs/cost-unit)
- §11 LOD/feedback serialization, authoring APIs, editor FB overlay
- WavePlayer sample offsets; steal validates stream before acquire
- Fingerprints + migrate; atomic `control_package`; debug metrics; stress suite; spectral suite + FFT `extra_cost_fn`
- Release mix-timing baselines; SpectralGate COLA/unity; GrainCloud `extra_cost_fn`

### This session
1. **RT-scope** — `SymphonyRealtimeScope` around mix / `CompiledGraph::execute` / VoiceManager + RTPC mix callbacks
2. Instrumented Symphony alloc, free, mutex, ObjectDB, compile, and dynamic-container sites (`symphony_rt_note`)
3. Tests: mix/execute report 0 violations; each kind is detectable with assert suppressor
4. Concurrent mix + swap/parameter/trigger/LOD/drain/stop stress (`THREADS_ENABLED`)
5. `get_rt_violation_count()` + debug metric `rt_violations`
6. **TSan** — `bin/godot.macos.editor.arm64.san`; Symphony suite reports no data races. GrainCloud µs/unit gate skipped under TSan/ASan.

---

## Suggested Next (priority) — M3 wrap-up docs

**Default: documentation / migration polish. Do not reopen RT-scope or TSan unless a new race appears.**

### Remaining M3
1. Review `MIGRATION.md` planned section (several items already landed; trim stale bullets).
2. Finish `user_guide.md` v1.7.1 changelog if any public API is still undocumented.
3. `game-template` is still out of scope.

```bash
# TSan (already green 2026-08-13)
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=1 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --source-file='*symphony*'
```

---

## Caveats / footguns

- Stress test that forces `global_limit_bytes=1` prints an expected `ERROR:` from `compile_graph`; `BudgetGuard` restores limits
- Never call `CompiledGraph::execute` with `p_num_frames > SYMPHONY_MICRO_BLOCK_SIZE`
- Micro-block: 32 on `__EMSCRIPTEN__`, else 64
- Oscillator high-freq AA can overshoot ~±3 (tests allow ±3.1)
- Editor LOD tier switch may auto-create empty variants — strip unused before ship
- Mix timing release baselines are machine/arch-specific; heavy host load can flake p99 under strict gates
- SpectralGate FFT magnitudes are **unnormalized** — `threshold_db` ≤0 still lets loud bins pass; tests use very quiet tones for attenuation
- GrainCloud `trigger_value` unused warning in `register_types` compile (pre-existing)
- TSan builds are slower; keep `module_raycast_enabled=no`; binary is `godot.macos.editor.arm64.san`
- GrainCloud µs/unit ≤2× gate is skipped under TSan/ASan (instrumentation distorts relative cost)
- Do not edit `review_version_1_7.md`

---

## How to Resume

1. Read this file + `improve_plan_1_7.md` §6 + Verification/TSan bullets  
2. `git checkout features/symphony_fixed` && `git status` (expect clean)  
3. Rebuild editor; confirm Symphony tests still green
4. TSan binary already exists as `bin/godot.macos.editor.arm64.san` if this machine built it
5. M3 wrap-up: trim stale `MIGRATION.md` planned bullets; `game-template` remains out of scope  

## Skills / knowledge

- Activate: `godot-gdscript`, `godot-gdextension-cpp`  
- Knowledge MCP (`user-rider`) may be down — continue from plan + code  
