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
| **M3** | In progress — functional + spectral/cost gates largely landed; TSan/RT-scope + release baselines remain |

**HEAD commits from this session (newest first):**

- (pending) — spectral suite depth + FFT extra_cost_fn
- `52cd2e8aaa` — memory stress + 10/30/50 mix timing
- `93ff25a112` — read-only metrics
- `5842cf7fce` — atomic control_package
- `0fb5639861` — fingerprints + audio-boundary migrate

**Tests (last run):** `49/49` Symphony cases pass

```bash
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'
# Prefer --test-case='*Symphony*' over '[Symphony]' (doctest char-class trap)
```

---

## Locked Decisions (do not reopen unless asked)

- Stop at each milestone for review; follow plan order
- One branch, multiple focused commits; breaking APIs OK
- CPU: warning 70% / critical 90%; memory 8 MiB/graph, global 128/64/32 (desktop/mobile/web)
- Budget rejection: compile error + metrics only (approach A)
- Transitions: 40 ms equal-power when admitted; else 64-sample single-graph fallback
- Crossfade tokens: 2 desktop / 1 mobile+web
- Keep auto-LOD; silence −120 dB + 2-block hysteresis; no large-history migration
- Tests “green enough to iterate”; TSan later
- **game-template code is out of scope** for this plan; document API migrations before release (done in MIGRATION + user_guide changelog)

---

## Landed and Reliable

### M2 close
- WavePlayer honors gate `sample_offset` in-block
- `play_event` resolves/validates stream **before** acquire/steal

### M3 — memory
- Compiler drains retirement, reserves `total_package_bytes` (arena + non-arena)
- SharedPCM: `try_reserve_shared` / `release_shared` (unique keys once)
- Global used = reserved + shared_pcm

### M3 — transition cost
- `OperatorDescriptor::cost_per_sample` (+ optional `extra_cost_fn`, unused so far)
- `estimated_cost_units` on CompileResult / CompiledGraph / PreparedGraphPackage
- Heavy defaults: PhaseVocoder 48, SpectralGate 32, GrainCloud 24, FDN 16
- VoiceManager EWMA µs/cost-unit; admission adds estimated incoming fraction → deny uses fallback

### §11 LOD/feedback
- Prefix serializer: `graph/...` + `lod/<tier>/...` + connection `is_feedback`
- APIs: `add_lod_variant`, `duplicate_main_to_lod`, `set_lod_variant`, `remove_lod_variant`, `get_lod_variant`, `has_lod_variant`, `estimate_tier_memory`, `validate_tier_compile`
- Editor: LOD tier selector, Dup→LOD, FB Toggle (UndoRedo), memory label
- Feedback overlay: **amber dashed edges + `FB` badge** (plan §11)
- Serialization round-trip tests green

### Fingerprints + audio-boundary migrate
- Package fingerprints: node_id + type hash + structural hash (exportable state size)
- `migrate_compatible_state` on audio adopt (equal-power start + fallback swap); ≤256 bytes
- `swap_graph` publish-only on main thread

### Handle-safe control path
- Atomic `control_package` published from `_install_package`
- `set_parameter` / `trigger` load-check-act with one retry on swap race

### Read-only metrics
- `SymphonyVoiceManager.get_debug_metrics()` + individual getters
- Transition / dropped-trigger / spectral-underflow / retirement destroyed+peak

### Memory stress + mix timing
- `tests/modules/test_symphony_stress.cpp`: global budget rejection without leak,
  failed compile preserves audible package, peak live ≤ current+outgoing+pending,
  retirement/reserved return to baseline, 10/30/50-node median/p99 mix timing

### Spectral suite + FFT cost
- PhaseVocoder unity (±0.5 dB at stretch=1), stretch=2 finite, underflow counter,
  cleanup/PFFFT release; SpectralGate finite processing; compile cost scales with FFT size
- `extra_cost_fn` wires `N·log2(N)·hops` for PhaseVocoder / SpectralGate

---

## Suggested Next (priority)

Still open from `improve_plan_1_7.md` M3 / deferred leftovers:

1. **TSan + RT-scope assertions** (§6 gate) — locked “later”
2. **Release-mode baseline comparison** for ≤5% median / ≤10% p99 gates
3. **SpectralGate COLA / threshold dB scale** — gate threshold clamped to ≤0 dB so strong bins rarely attenuate (test covers finite processing only)
4. **game-template** — implement documented API migration in that repo when ready (not this repo)

Ask user which slice to take first if unclear.

---

## Known leftovers / caveats

- Package fingerprints + audio-boundary migrate — **landed**
- Handle-safe `set_parameter` / `trigger` — **landed**
- Read-only metrics — **landed**
- Memory stress + mix timing — **landed** (absolute timings; no historical regression baseline yet)
- Spectral suite + `extra_cost_fn` — **landed**
- `extra_cost_fn` unused — **resolved** for PhaseVocoder / SpectralGate
- Editor LOD tier switch auto-creates empty variants if missing — authors should remove unused LODs before shipping (noted in user_guide)
- Oscillator high-freq AA can overshoot ~±3 (tests allow ±3.1)
- Micro-block: 32 on `__EMSCRIPTEN__`, else 64

---

## How to Resume

1. Read this file + skim `improve_plan_1_7.md` remaining M3 gates
2. Checkout `features/symphony_fixed`; confirm clean + rebuild if needed
3. Prefer next: release-mode timing baselines, or TSan when unlocked; SpectralGate dB/COLA polish optional
4. Keep updating `MIGRATION.md` / `user_guide.md` when APIs change; do not edit `review_version_1_7.md`
5. Prefer focused commits

## Knowledge / skills

- Activate: `godot-gdscript`, `godot-gdextension-cpp` (module uses ClassDB/GDCLASS patterns)
- Knowledge MCP (`user-rider` / audio-books / stk / faust) may be unavailable — continue from plan + code if so
