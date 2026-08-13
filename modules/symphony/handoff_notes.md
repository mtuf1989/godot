# Symphony Handoff Notes — Next Session

**Date:** 2026-08-13  
**Branch:** `features/symphony_fixed`  
**Plan source of truth:** `modules/symphony/improve_plan_1_7.md`  
**Do not edit:** `modules/symphony/review_version_1_7.md`  
**Migration log:** `modules/symphony/MIGRATION.md`

---

## Where We Are

| Milestone | Status |
|-----------|--------|
| **M1** | Done |
| **M2** | Closed |
| **M3** | In progress |

**Tests:** 34/34 Symphony cases pass  

```bash
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --source-file='*symphony*'
```

**Uncommitted** (ask before commit): M2 close + M3 memory/cost + §11 LOD/feedback.

---

## Done This Session (cumulative)

### M2 close
- WavePlayer `sample_offset`; `play_event` validate-before-steal

### M3 memory + cost
- `try_reserve(total_package_bytes)`; SharedPCM unique charge; retirement drain
- `estimated_cost_units` + EWMA admission for crossfade

### §11 LOD/feedback authoring
- Prefix serializer: `graph/...` + `lod/<tier>/...` + `is_feedback`
- APIs: add/duplicate/set/remove/get/has LOD variants; `estimate_tier_memory`; `validate_tier_compile`
- Editor: LOD tier OptionButton, Dup→LOD, FB Toggle (UndoRedo), memory label
- Feedback visual: connection activity highlight (dashed amber/`FB` badge still approximate)
- Serialization round-trip tests

---

## Suggested Next

1. Polish feedback edge draw (true amber dashed + FB badge) if GraphEdit custom draw needed
2. Full read-only metrics (transition/trigger/spectral-underflow)
3. Memory stress + benchmarks + TSan/RT scope
4. Spectral suite depth
5. Package fingerprints + audio-boundary migrate mailbox
6. Docs / `user_guide.md` + game-template migration

---

## Known leftovers

- Feedback UI uses activity highlight, not full amber dashed + badge
- Main-thread `swap_graph` torn-read; `set_parameter`/`trigger` race
- Package fingerprints missing
- `extra_cost_fn` unused (fixed `cost_per_sample` only)
