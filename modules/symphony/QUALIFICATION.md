# Symphony procedural music — V1 qualification

Status as of 2026-10-09. **P8b** voice policy is done. Optional **P9** (perf convention / profiling / RTSan CI) and designer listening remain deferred.

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

## Deferred with P9 / listening

| Gate | Why deferred |
|---|---|
| State-in-locals + FeedbackPath zero-extra + compile cache | **P9** |
| 30-minute release callback P50/P95/P99 at 44.1/48 kHz | **P9** performance convention |
| RTSan CI | **P9** (document-only follow-up) |
| Designer listening / audition scene polish | Manual; not blocked on code |
| Full stretch fixture matrix (tempo endpoints × ±6 st WAV renders) | Needs authored WAV assets + listening |
| Lifetime denser cases T2/T4/… | Optional coverage (Progress notes) |

## How to re-run

```bash
# Godot (from engine root)
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'

# game-template
node addons/agent_cli/gd.mjs test test/addons/symphony_audio/procedural_composer_test.gd
node addons/agent_cli/gd.mjs test test/addons/symphony_audio/procedural_music_g2_test.gd
node addons/agent_cli/gd.mjs test test/addons/symphony_audio/procedural_music_v1_test.gd
```

## Public API surface (procedural)

**Native:** `AudioStreamSymphonyMusic` / `AudioStreamPlaybackSymphonyMusic` — `submit_phrase`, `replace_future`, `schedule_tempo`, `schedule_track_gain`, `get_transport_snapshot`, `drain_status`.

**GDScript:** `MusicSystem.load_score(score, seed)`, `set_state`, `set_intensity`, `request_tempo`, `request_key`; score resources under `addons/symphony_audio/procedural/`; demo via `DemoScoreFactory.make_demonstration_score()`.

See `user_guide.md` (MusicNoteInput + procedural stream) and `proc-music-plan.md` §5 for the full acceptance list.
