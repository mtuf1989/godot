# Symphony procedural music — V1 qualification

Status as of 2026-10-09. **P8b** and **P9** code gates are done. Designer listening, 30‑minute release profiling, and RTSan CI remain deferred.

## Automated gates (closed)

| Gate | Evidence |
|---|---|
| C++ Symphony suite | `bin/godot… --test --test-case='*Symphony*'` — includes N1–N3, MusicStream, V1 timing |
| Sample-accurate note offsets 0/1/31/32/63 | `test_symphony_music_v1.cpp` |
| Callback size 32 vs 64 sequence | `test_symphony_music_v1.cpp` |
| Phrase `replace_future` + VITAL | `test_symphony_music_v1.cpp` |
| Composer determinism + locked + recording filter | `procedural_composer_test.gd` |
| ≥1000-seed batch (range / reproduce) | `procedural_music_v1_test.gd` |
| Demo score (bass/chords/melody/perc/bed + sections) | `DemoScoreFactory` |
| MusicSystem PROCEDURAL load + adaptive states | `procedural_music_g2_test.gd`, `procedural_music_v1_test.gd` |
| TSan baseline | Progress: 1 warning (`DynamicRangeHigh`); no new Symphony-frame races after P1b |

## Closed with P8b

| Gate | Evidence |
|---|---|
| Category caps + N8 forced victim + N12 quietest | `test_symphony_voice.cpp` `[P8b]` |
| GDScript admit tiers/caps | `audio_manager_behavior_test.gd` |

## Closed with P9

| Gate | Evidence |
|---|---|
| State-in-locals + FeedbackPath zero-extra + compile cache | `test_symphony_p9.cpp` |

## Closed: denser lifetime + profile harness

| Gate | Evidence |
|---|---|
| Lifetime T2/T4/T6–T9/T11/T13–T16 | `test_symphony_lifetime.cpp` |
| Callback P50/P95/P99 smoke (44.1/48 kHz) | `test_symphony_profile.cpp` (3 s always) |
| Full 30‑min gate | Optional: `SYMPHONY_PROFILE_SECONDS=1800` + prefer `template_release` |

## Deferred (need human / Linux)

| Gate | Why deferred |
|---|---|
| RTSan CI | Needs Linux LLVM ≥ 20 CI (documented in `MIGRATION.md`) |
| Designer listening / audition scene polish | Manual |
| Full stretch fixture matrix | Authored WAV assets + listening |
| Accept 30‑min P99 gate numbers | Run harness above; human sign-off of capacity |

## How to re-run

```bash
# Godot (from engine root)
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'

# Optional 30-min callback profile (prefer template_release binary)
SYMPHONY_PROFILE_SECONDS=1800 bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*Profile*30*'

# game-template
node addons/agent_cli/gd.mjs test test/addons/symphony_audio/procedural_composer_test.gd
node addons/agent_cli/gd.mjs test test/addons/symphony_audio/procedural_music_g2_test.gd
node addons/agent_cli/gd.mjs test test/addons/symphony_audio/procedural_music_v1_test.gd
```

## Public API surface (procedural)

**Native:** `AudioStreamSymphonyMusic` / `AudioStreamPlaybackSymphonyMusic` — `submit_phrase`, `replace_future`, `schedule_tempo`, `schedule_track_gain`, `get_transport_snapshot`, `drain_status`.

**GDScript:** `MusicSystem.load_score(score, seed)`, `set_state`, `set_intensity`, `request_tempo`, `request_key`; score resources under `addons/symphony_audio/procedural/`; demo via `DemoScoreFactory.make_demonstration_score()`.

See `user_guide.md` (MusicNoteInput + procedural stream) and `proc-music-plan.md` §5 for the full acceptance list.
