# Symphony Handoff Notes — Next Session

**Date:** 2026-08-13  
**Branch:** `features/symphony_fixed`  
**Plan source of truth:** `modules/symphony/improve_plan_1_7.md`  
**Do not edit:** `modules/symphony/review_version_1_7.md`  
**Migration log:** `modules/symphony/MIGRATION.md`  
**User guide:** `modules/symphony/user_guide.md` (v1.7.1 changelog started)

---

## Where We Are

| Milestone | Status |
|-----------|--------|
| **M1** | Done |
| **M2** | Closed |
| **M3** | Nearly complete — remaining: TSan/RT-scope (locked later) |

**HEAD (newest first) — prior M3 commits:**

- `e9b517d919` — Handoff notes for M3 close-out
- `ff4c2235ce` — Spectral suite depth + FFT `extra_cost_fn` (`N·log2(N)·hops`)
- `52cd2e8aaa` — Memory stress + 10/30/50-node mix timing
- `93ff25a112` — Read-only transition / trigger / spectral / retirement metrics
- `5842cf7fce` — Atomic `control_package` for `set_parameter` / `trigger`
- `0fb5639861` — Package fingerprints + audio-boundary state migrate

**Tests:**

```bash
# Editor (soft timing ceiling only)
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'

# Release regression gates (median ≤+5%, p99 ≤+10% vs stored baselines)
scons platform=macos target=template_release arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.template_release.arm64 --headless --test --test-case='*Mix timing*'
# Prefer --test-case='*Symphony*' over '[Symphony]' (doctest char-class trap)
```

**Release mix timing baselines** (3-trial median of `64 × (32×512)` frames @ 48 kHz, macos arm64):

| Graph | baseline median | baseline p99 |
|-------|----------------:|-------------:|
| 10-node | 209 µs | 335 µs |
| 30-node | 1254 µs | 1747 µs |
| 50-node | 2420 µs | 2790 µs |

Per-512f equivalent ≈ median/32 (~6.5 / 39 / 76 µs).

---

## Locked Decisions (do not reopen unless asked)

- Stop at each milestone for review; follow plan order
- One branch, multiple focused commits; breaking APIs OK
- CPU: warning 70% / critical 90%; memory 8 MiB/graph, global 128/64/32 (desktop/mobile/web)
- Budget rejection: compile error + metrics only (approach A)
- Transitions: 40 ms equal-power when admitted; else 64-sample single-graph fallback
- Crossfade tokens: 2 desktop / 1 mobile+web
- Keep auto-LOD; silence −120 dB + 2-block hysteresis; no large-history migration
- Tests “green enough to iterate”; **TSan later**
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

### This session
1. **Release timing baselines** — `template_release` hard-fails 10/30/50-node mix timing (`32×512f` batches, 3-trial median) at plan ≤+5% median / ≤+10% p99 vs stored macos-arm64 constants. Editor keeps soft ceiling only. Expect flake under heavy host load.
2. **SpectralGate polish** — COLA `1/Σw²` table (parity with PhaseVocoder / review 6.3); open-threshold unity ±0.5 dB; threshold clamp ≤0 dB covered by attenuation test; arena estimate corrected to 10N floats.

---

## Suggested Next (priority)

1. **TSan + RT-scope assertions** (§6) — only when unlocked (currently “TSan later”)  
2. **GrainCloud / other heavies `extra_cost_fn`** — only if admission still under-calibrated  
3. **game-template** — separate repo when ready  

Ask user which slice if unclear (default: stop for M3 review unless TSan unlocked).

---

## Caveats / footguns

- Stress test that forces `global_limit_bytes=1` prints an expected `ERROR:` from `compile_graph`; `BudgetGuard` restores limits (and recovers tiny leftover limits from aborted runs)
- Never call `CompiledGraph::execute` with `p_num_frames > SYMPHONY_MICRO_BLOCK_SIZE` (hang/corruption); stress timing loops in micro-blocks
- Micro-block: 32 on `__EMSCRIPTEN__`, else 64
- Oscillator high-freq AA can overshoot ~±3 (tests allow ±3.1)
- Editor LOD tier switch may auto-create empty variants — strip unused before ship
- Mix timing release baselines are machine/arch-specific and use **strict** +5%/+10%
  gates on `template_release`; recalibrate constants in `test_symphony_stress.cpp` if
  builders or the reference Mac change. Heavy host load can flake p99.
- Do not edit `review_version_1_7.md`

---

## How to Resume

1. Read this file + skim remaining M3 gates in `improve_plan_1_7.md`  
2. `git checkout features/symphony_fixed` && `git status`  
3. Rebuild if binary stale; run Symphony tests (commands above)  
4. Prefer next: ask whether to unlock TSan, or stop for M3 review  
5. Update `MIGRATION.md` / `user_guide.md` on API changes; focused commits  

## Skills / knowledge

- Activate: `godot-gdscript`, `godot-gdextension-cpp`  
- Knowledge MCP (`user-rider`) may be down — continue from plan + code  
