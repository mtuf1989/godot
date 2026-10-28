# Symphony qualification (B0–B8)

## Measured record (2026-10-27)

Production-readiness stays **pending**. This record is the local half of sections 4 and 5: three Web templates, one headed Chrome run per export mode, symbolized ASan/UBSan, symbolized TSan, and the native 1,800-second soaks. Android Chrome, iOS Safari, Linux RTSan, Firefox, Safari measurements, and designer listening were not run.

Engine commit `fb1205eaa752` on `master`, dirty working tree. The exported pages print `Godot Engine v4.8.dev.custom_build.fb1205eaa (2026-10-11 16:16:10 UTC)`; that stamp is older than these binaries. Host: Apple M5, 10 cores, 32 GiB RAM, macOS 26.6 (25G72). Chrome 156.0.8078.48. Its user agent says Intel Mac OS X 10_15_7. Firefox 159.0 is installed and was not launched. Safari 26.6 was opened on the threaded page and produced no measurements.

Native macOS builds must use Apple clang. A shell that has sourced Emscripten puts `emcc`'s clang first, and `scons` then fails with an unknown target. Web builds need `source /Users/luong.pham/Work/emsdk/emsdk_env.sh` (`emcc` 6.0.12). These Web templates were not built with the CI extras `dev_mode=yes`, `debug_symbols=no`, or `use_closure_compiler=yes`.

### Web templates and PFFFT

| Build | Artifact | PFFFT object |
|---|---|---|
| `threads=yes wasm_simd=yes` | `bin/godot.web.template_release.wasm32.zip` (9.2M) | `pffft_simd_size` is `i32.const 4`; object string `SSE1` |
| `threads=no wasm_simd=yes` | `bin/godot.web.template_release.wasm32.nothreads.zip` (9.1M) | `i32.const 4`; object string `SSE1` |
| `threads=no wasm_simd=no extra_suffix=scalar` | `bin/godot.web.template_release.wasm32.nothreads.scalar.zip` (9.3M) | `i32.const 1`; object string `Scalar` |

The arch string is in the object file. `template_release` LTO drops it from the linked wasm, so `strings` on the zip is not the check. The scalar zip is a smoke build, not a qualification preset. The native editor test still reports `pffft arch=Scalar simd_size=1`, which is the arm64 kernel.

### Native 1,800-second soaks

Binary: `bin/godot.macos.template_release.arm64` (110M, built 2026-10-27 22:10 with `tests=yes debug_symbols=yes`). Command: `SYMPHONY_PROFILE_SECONDS=1800` and `--test-case='*Symphony*Profile*30*'`. Both spectral cases use that value with no shorter cap. The run failed the written gate.

The harness times wall-clock callbacks in a tight loop at 48 kHz / 128 frames. The deadline is 2666 µs. A miss is one callback slower than that deadline, not a device underrun. The P99 check is `p99 * 2 < deadline`. The zero-miss check is separate and is what failed.

| Workload | n | prepare µs | P50 | P95 | P99 | P99 / deadline | Misses | Symphony bytes after warmup | Peak | After teardown |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| web-spectral | 75,888,977 | 1890 | 29 µs | 33 µs | 43 µs | 0.016129 | 1,475 | 829,848 | 851,960 | 47,392 |
| desktop-spectral | 31,066,279 | 1461 | 43 µs | 100 µs | 110 µs | 0.0412603 | 1,185 | 1,886,864 | 1,886,864 | 142,176 |

Doctest: 2 cases, 0 passed, 2 failed, 70 of 72 assertions passed. Both failures are `CHECK(misses == 0)`. P99 stayed under half the deadline for both workloads. Used bytes did not grow from warmup to the end of either 1,800-second loop. Teardown released the workload. The two post-teardown figures are not the same (47,392 then 142,176), so this run does not show a single shared baseline. Process RSS is the harness sample vector plus the engine: it started at 11,280 KB, peaked at 646,000 KB while sorting the first vector (30:02), dropped to 58,752 KB after that teardown (30:32), and was 278,800 KB at 59:34 while the second vector was still alive. RSS is not the Symphony budget.

There is no Web 1,800-second soak. The browser pages do not run this harness.

### Exported Chrome

Exports used the qualification presets in `game-template`. `ProjectSettings.get_setting_with_override` applied `audio/driver/output_latency` of 100 ms (single) and 50 ms (threaded). `get_setting` without the override stayed at the engine default, 15 ms. Pages were served with COOP `same-origin`, COEP `require-corp`, and CORP `cross-origin`. Headless Chrome has no WebGL2, so Godot refused to start; the runs below are headed Chrome with its own user-data directory.

The main scene is an empty `Node2D`. Autoloads run. The probe prints `workload=exported-main-scene`. Mix percentiles, underruns, and voice counts were not measured in the browser. `AudioServer.get_output_latency()` and Chrome `baseLatency` / `outputLatency` are reported estimates. End-to-end latency was not measured: there was no input-to-audio loopback. Resume counters (expired note-on or clip-on catch-up, future phrases pending, generation-map size, queue occupancy) are not implemented, so resume-backlog stays **unverified**. That leaves the resume gate and the end-to-end latency gate open.

| | Threaded | Single-thread |
|---|---|---|
| Page | `http://127.0.0.1:8072/index.html` | `http://127.0.0.1:8071/index.html` |
| Build line | Emscripten 6.0.12, multi-threaded | Emscripten 6.0.12, single-threaded |
| Requested latency | 50 ms | 100 ms |
| `get_output_latency()` at startup | 0.0426666662097 s (2048/48000) | 0.0853333324194 s (4096/48000) |
| After 1 s | 0.07199999690056 s | not printed |
| Chrome AudioContext | `running`, `baseLatency` 0.005333333333333333 s, `outputLatency` 0.024 s, 48000 Hz | not read; DevTools commands timed out |
| Symphony memory | 0 / 0 bytes | 0 / 0 bytes |
| 10×5 s background | audio stayed `running` on every cycle | not run |

Threaded recovery to visible plus `running`, in milliseconds: 45, 34, 23, 13, 49, 233, 24, 33, 27, 40. Hiding the page did not suspend the `AudioContext`, so catch-up playback had nothing to show. The single-thread page printed startup and then stopped servicing the browser: `after_1s` never appeared and DevTools evaluation timed out. The threaded page stayed responsive. That stall was not root-caused.

### Sanitizers

ASan/UBSan and TSan both overwrite `bin/godot.macos.editor.arm64.san`. The ASan binary was copied to `/tmp/symphony-qual/godot.macos.editor.arm64.asan` before the TSan link. Both were symbolized (`debug_symbols=yes`) editor builds with `tests=yes`.

ASan/UBSan (`ASAN_OPTIONS=halt_on_error=0:print_stacktrace=1:abort_on_error=0`, `UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1`): **364/364** passed, 2,601,789 assertions, process exit 0. AddressSanitizer summaries: 0. Leak summaries: 0. UndefinedBehaviorSanitizer summaries: 417, at 314 sites. They are misaligned constructor and member calls on objects that contain `alignas(64)` atomics (`SymphonyPublishedU64`, the SPSC ring, and `SymphonySeqLock`). `memnew` does not return 64-byte-aligned storage. The first report is `SymphonyVoicePool` at `register_types.cpp:211`. This is a Symphony alignment requirement on Godot's allocator. The zero-UB gate does not pass.

TSan (`TSAN_OPTIONS="halt_on_error=0 print_stacktrace=1 exitcode=0"`): **364/364** passed, 2,601,789 assertions, process exit 0, `ThreadSanitizer: reported 2 warnings`. A first run without `exitcode=0` printed one warning, then the runtime aborted (exit 134) after doctest had already printed SUCCESS. The second warning is timing-dependent and was absent from that run. The two reports:

1. Symphony. `[Symphony][B0] Mixer-thread scaffold` reads `SymphonyMusicTransport::tempo_count` on the main thread through `_test_transport_segment_count()` (`audio_stream_playback_symphony_music.cpp:848`) while the mixer thread writes it in `SymphonyMusicTransport::reset`, called from `_audio_restart()` (`audio_stream_playback_symphony_music.cpp:974`). The published path is the scheduler seqlock. This test helper does not use it.
2. Upstream dummy driver, provoked by a Symphony test. `[Symphony][Foundation][Audio] render_offline ignores a 44.1k device` calls `AudioDriverDummy::set_mix_rate` (`servers/audio/audio_driver_dummy.cpp:105`) while `AudioDriverDummy::thread_func` reads `mix_rate` (`audio_driver_dummy.cpp:59`) before taking the driver lock.

The zero-race gate does not pass. The October 11 TSan note of three warnings belongs to the historical table below, not to this binary. Linux RTSan was not run.

### Still open

| Gate | State |
|---|---|
| Desktop Firefox | Installed 159.0. No exported run. |
| Desktop Safari / WebKit | Safari opened the threaded URL. `safaridriver` needs an admin password. JavaScript from Apple Events is off. No latency, cycle, or console record. |
| Single-thread resume cycles | Blocked by the page stall above. |
| Resume backlog | Counters are absent. Recorded as unverified. |
| End-to-end latency | Not measured. |
| In-browser mix percentiles, underruns, workload counts | Native harness only. |
| Web 1,800-second soak | Not run. |
| Android Chrome, iOS Safari | Not run. |
| Linux RTSan, including third-party allocation | Not run. Needs Linux LLVM ≥ 20. |
| Listening | `game-template/scenes/audio/procedural_music_audition.tscn` was not auditioned. |

The tables below are the closed B0–B8 record as of 2026-10-11. The 353-case counts are that record, not this tree. An editor smoke earlier on 2026-10-27 reported 363/363 before the 30-minute case was split into the two soak cases measured above.

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
| Full 30‑min gate | Measured 2026-10-27 on `template_release`. P99 passed; `misses == 0` failed. See the record above. |

## Deferred (need human / Linux)

| Gate | Why deferred |
|---|---|
| RTSan CI | Needs Linux LLVM ≥ 20 CI (documented in `MIGRATION.md`) |
| Designer listening / audition scene polish | Manual — open `game-template/scenes/audio/procedural_music_audition.tscn` (F6) |
| Full stretch fixture matrix | Authored WAV assets + listening |
| Accept 30‑min P99 gate numbers | 2026-10-27 release run: P99 43 µs and 110 µs of 2666 µs, with 1,475 and 1,185 tight-loop misses. The written zero-miss check failed. |

## How to re-run

```bash
# Godot (from engine root)
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'

# TSan (music + lifetime stress). ASan/UBSan uses the same .san name; copy the binary aside before relinking.
# debug_symbols=yes matches the 2026-10-27 record. exitcode=0 keeps a reported race from aborting the process.
scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes debug_symbols=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=0 print_stacktrace=1 exitcode=0" bin/godot.macos.editor.arm64.san --headless --test --test-case='*Symphony*'

# Native release soak. Both spectral cases honor SYMPHONY_PROFILE_SECONDS; there is no 120s cap.
# Use a shell whose CC/CXX are Apple clang, not Emscripten's.
SYMPHONY_PROFILE_SECONDS=1800 bin/godot.macos.template_release.arm64 --headless --test --test-case='*Symphony*Profile*30*'

# Web templates (after sourcing emsdk 6.0.12). Confirm PFFFT on the .o, not the linked wasm.
# scons platform=web target=template_release threads=yes wasm_simd=yes
# scons platform=web target=template_release threads=no wasm_simd=yes
# scons platform=web target=template_release threads=no wasm_simd=no extra_suffix=scalar

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
