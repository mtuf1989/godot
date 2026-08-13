# Symphony Handoff Notes — Next Session

**Date:** 2026-08-13  
**Branch:** `features/symphony_fixed` (working tree clean after this note)  
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
| **M3** | Nearly complete — remaining: TSan/RT-scope (locked later), release timing baselines, optional SpectralGate polish |

**HEAD (newest first) — this session’s M3 commits:**

- `ff4c2235ce` — Spectral suite depth + FFT `extra_cost_fn` (`N·log2(N)·hops`)
- `52cd2e8aaa` — Memory stress + 10/30/50-node mix timing
- `93ff25a112` — Read-only transition / trigger / spectral / retirement metrics
- `5842cf7fce` — Atomic `control_package` for `set_parameter` / `trigger`
- `0fb5639861` — Package fingerprints + audio-boundary state migrate

**Tests (last run):** `49/49` Symphony cases pass

```bash
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'
# Prefer --test-case='*Symphony*' over '[Symphony]' (doctest char-class trap)
```

**Editor-build mix timings (512 frames @ 48 kHz, absolute — not release baselines):**

| Graph | median | p99 |
|-------|--------|-----|
| 10-node | ~6 µs | ~7 µs |
| 30-node | ~40 µs | ~50–70 µs |
| 50-node | ~75–80 µs | ~90–100 µs |

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

### Earlier M3 (pre-this-session, still good)
- Exact package memory charging; SharedPCM unique charging
- Calibrated crossfade admission (EWMA µs/cost-unit)
- §11 LOD/feedback serialization, authoring APIs, editor FB overlay
- WavePlayer sample offsets; steal validates stream before acquire

### This session
1. **Fingerprints + migrate** — `CompiledGraph::operator_types`; package fingerprints; `migrate_compatible_state` on equal-power start + fallback swap; `swap_graph` publish-only
2. **Control path** — `std::atomic<PreparedGraphPackage*> control_package`; load-check-act + one retry
3. **Metrics** — `SymphonyVoiceManager::get_debug_metrics()` + getters (memory snapshot, transitions, dropped triggers, spectral underflow, retirement pending/peak/destroyed)
4. **Stress** — `tests/modules/test_symphony_stress.cpp` (budget reject, audible preserve, live ≤3 delta, retirement teardown, mix timing)
5. **Spectral** — real suite (replaced scaffold); PV unity ±0.5 dB @ stretch=1; stretch=2 finite; cleanup; cost scales with FFT; `extra_cost_fn` on PV/SG
6. PFFFT includes use `modules/symphony/thirdparty/pffft/pffft.h` so tests can compile spectral headers

---

## Suggested Next (priority)

1. **Release-mode timing baselines** — rebuild `target=template_release` (or equivalent), record 10/30/50 median/p99, wire ≤5% / ≤10% gates vs stored baseline  
2. **TSan + RT-scope assertions** (§6) — only when unlocked (currently “TSan later”)  
3. **SpectralGate polish (optional)** — threshold clamped to ≤0 dB so strong bins rarely attenuate; no COLA table (unlike PhaseVocoder); tests only assert finite/non-NaN  
4. **GrainCloud / other heavies `extra_cost_fn`** — only if admission still under-calibrated  
5. **game-template** — separate repo when ready  

Ask user which slice if unclear; default to (1) if continuing M3 close-out.

---

## Caveats / footguns

- Stress test that forces `global_limit_bytes=1` prints an expected `ERROR:` from `compile_graph`; `BudgetGuard` restores limits (and recovers tiny leftover limits from aborted runs)
- Never call `CompiledGraph::execute` with `p_num_frames > SYMPHONY_MICRO_BLOCK_SIZE` (hang/corruption); stress timing loops in micro-blocks
- Micro-block: 32 on `__EMSCRIPTEN__`, else 64
- Oscillator high-freq AA can overshoot ~±3 (tests allow ±3.1)
- Editor LOD tier switch may auto-create empty variants — strip unused before ship
- Do not edit `review_version_1_7.md`

---

## How to Resume

1. Read this file + skim remaining M3 gates in `improve_plan_1_7.md`  
2. `git checkout features/symphony_fixed` && `git status` (expect clean)  
3. Rebuild if binary stale; run Symphony tests (command above)  
4. Prefer next: **release baselines** (or ask)  
5. Update `MIGRATION.md` / `user_guide.md` on API changes; focused commits  

## Skills / knowledge

- Activate: `godot-gdscript`, `godot-gdextension-cpp`  
- Knowledge MCP (`user-rider`) may be down — continue from plan + code  
