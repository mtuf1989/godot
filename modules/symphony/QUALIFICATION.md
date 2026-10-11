# Symphony qualification (B0–B8)

Status as of 2026-10-11. Automated gates for P0–P10, music N0–V1, and bug-fix phases **B0–B8** are closed. Designer listening, 30‑minute release profiling, and RTSan CI remain deferred.

## Automated gates (closed)

| Gate | Evidence |
|---|---|
| C++ Symphony suite | `bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'` — **353/353** |
| Per-thread alloc counter + `SYMPHONY_CHECK_NO_ALLOC` | `test_symphony_b0.cpp` `[Symphony][B0]` |
| Allocation-free music mix (5 s, notes + tempo + gain) | `test_symphony_music_boundary.cpp` (`SYMPHONY_CHECK_NO_ALLOC` around `mix_graph_rate`) |
| Phrase-boundary handoff, generation-scoped notes, late phrase | `test_symphony_music_boundary.cpp` `[Symphony][B1]` |
| Sample-accurate note offsets 0/1/31/32/63 | `test_symphony_music_v1.cpp` |
| Callback size 32 vs 64 sequence | `test_symphony_music_v1.cpp` |
| Phrase `replace_future` + VITAL | `test_symphony_music_v1.cpp` |
| Command ring / phrase slots / generation `replace_future` | `test_symphony_music_b2.cpp` `[Symphony][B2]` |
| Clip tempo-as-speed, Catmull-Rom resample, unclaimed return | `test_symphony_music_n3.cpp` |
| FDN/GrainCloud layout keys, PhaseVocoder 8192, TriggerBuffer clamp | `test_symphony_b4.cpp` `[Symphony][B4]` |
| Oscillator/SVFilter reset, reverb last_params, trigger_at order, BeatClock | `test_symphony_b5.cpp` `[Symphony][B5]` |
| Music `render_offline` duration, repeatability, player refusal | `test_symphony_music_stream.cpp` `[Symphony][B7]` |
| Composer determinism, per-bar roots, key shift, overlap polyphony | `procedural_composer_test.gd` |
| ≥1000-seed batch (range / reproduce) | `procedural_music_v1_test.gd` |
| Activation-driven submit, fallback, `set_state` signals, `stop` stream | `procedural_music_controller_test.gd` |
| MusicSystem PROCEDURAL load + adaptive states | `procedural_music_g2_test.gd`, `procedural_music_v1_test.gd` |
| Offline WAV duration + byte-identical repeat | `procedural_music_g2_test.gd` `test_controller_offline_ledger_helper` |
| Demo score (bass/chords/melody/perc/bed + sections) | `DemoScoreFactory` |
| Callback P50/P95/P99 smoke (44.1/48 kHz, 3 s) | `test_symphony_profile.cpp` |
| TSan full `*Symphony*` + music/lifetime stress | Rebuilt `.san` 2026-10-11: **353/353** passed; `ThreadSanitizer: reported 3 warnings` (1 = known `DynamicRangeHigh`; 2 = mixer-thread heap races with unresolved frames, same class as the P0 baseline). See `MIGRATION.md`. |

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
| Designer listening / audition scene polish | Manual — open `game-template/scenes/audio/procedural_music_audition.tscn` (F6) |
| Full stretch fixture matrix | Authored WAV assets + listening |
| Accept 30‑min P99 gate numbers | Run harness above; human sign-off of capacity (release P99 ≈ 21 µs; rare OS miss spikes) |

## How to re-run

```bash
# Godot (from engine root)
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'

# TSan (music + lifetime stress)
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=0 print_stacktrace=1" bin/godot.macos.editor.arm64.san --headless --test --test-case='*Symphony*'

# Optional 30-min callback profile (prefer template_release binary)
SYMPHONY_PROFILE_SECONDS=1800 bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*Profile*30*'

# game-template
GODOT_BIN=/path/to/bin/godot.macos.editor.arm64 node addons/agent_cli/gd.mjs test test/addons/symphony_audio

# Designer listening (editor, not headless)
# Open scenes/audio/procedural_music_audition.tscn → F6
# Uses DemoScoreFactory + MusicSystem.load_score / set_state / set_intensity / request_tempo / request_key
```

## Public API surface (procedural)

**Native:** `AudioStreamSymphonyMusic` / `AudioStreamPlaybackSymphonyMusic` — `submit_phrase`, `replace_future`, `schedule_tempo`, `schedule_track_gain`, `register_clip_wav`, `get_transport_snapshot`, `drain_status`, `render_offline`.

**GDScript:** `MusicSystem.load_score(score, seed)`, `set_state`, `set_intensity`, `request_tempo`, `request_key`; `ProceduralMusicController.render_offline_wav`; score resources under `addons/symphony_audio/procedural/`; demo via `DemoScoreFactory.make_demonstration_score()`.

See `user_guide.md` (MusicNoteInput + procedural stream) and `bugfix-plan.md` for B0–B8.

The no-allocation claim on the music mix path is the B1 test in `test_symphony_music_boundary.cpp` (`SYMPHONY_CHECK_NO_ALLOC` over 5 s of `mix_graph_rate` with notes, tempo events and gain events).
