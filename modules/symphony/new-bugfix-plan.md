# Symphony hardening for real-time games and Web

## Status (2026-10-28)

Uncommitted in `godot` and `game-template`. Do not commit unless asked. Production-readiness stays pending. Full measurements are in `QUALIFICATION.md`.

Sections 2–3 are implemented: ownership, memory budget, scheduler, RTPC, addon lifecycle, offline rendering, and the deterministic tests that cover them.

Section 4 code is in the tree. `wasm_simd` selects the PFFFT SSE1 kernel, callback publication uses 32-bit atomics and `SymphonyPublishedU64`, and the profile harness covers web and desktop baselines plus spectral variants. Both 1,800-second spectral cases honor `SYMPHONY_PROFILE_SECONDS` with no shorter cap. Qualification presets request 100 ms single-thread and 50 ms threaded. Stream playback was already set on Symphony-owned players.

Local gates, run 2026-10-27 on commit `fb1205eaa752`:

| Gate | Result |
|---|---|
| Three Web templates, Emscripten 6.0.12 | Built. SIMD PFFFT objects return size 4 and contain `SSE1`. The scalar object returns size 1 and contains `Scalar`. |
| Native 1,800 s soaks, `template_release` | P99 passed: web-spectral 43 µs, desktop-spectral 110 µs, deadline 2666 µs. Zero-miss failed: 1,475 and 1,185 tight-loop spikes. Symphony bytes stayed flat during each run. Post-teardown totals differ (47,392 then 142,176). |
| Headed Chrome, threaded | Request 50 ms. Startup output-latency estimate 0.0427 s. Ten 5-second hides left `AudioContext` running. Resume backlog unverified. End-to-end latency not measured. |
| Headed Chrome, single-thread | Request 100 ms. Startup output-latency estimate 0.0853 s, then the page stopped servicing the browser. Resume cycles were not run. |
| Symbolized ASan/UBSan | 364/364 passed. 0 ASan reports. 417 UBSan reports: `alignas(64)` objects allocated with `memnew`. |
| Symbolized TSan | 364/364 passed. 2 warnings: the B0 test helper reads `tempo_count` during `_audio_restart`; upstream `AudioDriverDummy::set_mix_rate` races its mixer thread. |

Still open: Firefox, Safari measurements, the ten-hide resume gate, a Web 1,800-second soak, in-browser mix and underrun counts, end-to-end latency, Android Chrome, iOS Safari, Linux RTSan, and designer listening. The single-thread page stall is closed (N2, 2026-10-28).

`rope_chain` level: the gain param that was 0.4 is now 0.25, and its modulation pin overrides that param. The level that reaches the output is MapRange id 19 (`material` → final gain pin); its `out_min` / `out_max` are scaled by 0.65.

Leave these alone unless the next session is still inside sections 2–3:

- Clip identity is context handle (pool slot + reuse generation) plus musical identity (playback epoch + phrase generation + track id + clip event id assigned from the pre-sort ordinal). Activation requires both, and the scheduled tick.
- Controller generation map is capped at 8. Dialogue `queue_limit = -1` means project setting `audio/symphony/dialogue_queue_limit` (default 32). Offline chunks are at most 4096 stereo frames at 48 kHz.

## Next steps — agent execution handoff

**N4 is the next implementation batch.** N1, N2, and N3 passed on 2026-10-28; the execution log has the binary identity and the browser reports. Leave N4–N5 unchecked until each task’s exit criteria pass. Production-readiness stays pending.

### Where the work stands

| Kind | Item | What is already known |
|---|---|---|
| Completed implementation | Sections 2–3 | Ownership, memory budget, scheduler, RTPC, addon lifecycle, offline rendering, and their deterministic tests are in the working tree. |
| Completed implementation | Section 4 code | `wasm_simd` selects the PFFFT SSE1 kernel, callback publication uses 32-bit atomics and `SymphonyPublishedU64`, and the profile harness covers web and desktop baselines plus spectral variants. Both 1,800-second spectral cases honor `SYMPHONY_PROFILE_SECONDS`. Qualification presets request 100 ms single-thread and 50 ms threaded. Stream playback is set on Symphony-owned players. The qualification gates below are still open. |
| Known failure | UBSan, 417 reports | Objects containing `alignas(64)` atomics are constructed through Godot `memnew`. First report: `SymphonyVoicePool` at `register_types.cpp:211`. Suite result that day: 364/364, 0 ASan reports, process exit 0. The zero-UB gate failed. |
| Known failure | TSan, Symphony | `[Symphony][B0] Mixer-thread scaffold` reads `SymphonyMusicTransport::tempo_count` on the main thread through `_test_transport_segment_count()` while the mixer thread writes it in `reset` from `_audio_restart()`. The published path is the scheduler seqlock. |
| Known failure, upstream | TSan, dummy driver | `[Symphony][Foundation][Audio] render_offline ignores a 44.1k device` calls `AudioDriverDummy::set_mix_rate` while `AudioDriverDummy::thread_func` reads `mix_rate` before the driver lock. Keep this report, its stack, and the 44.1 kHz regression. |
| Known failure | Native soak misses | `template_release`, 1,800 s. P99 passed (web-spectral 43 µs, desktop-spectral 110 µs, deadline 2666 µs). `CHECK(misses == 0)` failed: 1,475 and 1,185 tight-loop spikes. A spike is a wall-clock callback slower than the deadline inside the harness loop. It is a harness miss, and a device underrun is a separate measurement. Post-teardown Symphony bytes differed (47,392 then 142,176). |
| Fixed, 2026-10-28 | Single-thread Web stall | `GLog.start_session` drained `Semaphore.try_wait()` on the first idle frame. The no-thread stub returned true forever, so the loop never ended. Physics had already returned. N2 closed it. |
| Unmeasured | Browser workload | The probe observes an empty main scene and prints `workload=exported-main-scene`. Mix timing, underruns, active counts, and resume backlog are unmeasured. |
| Unmeasured | Sustained Web and latency | No Web 1,800-second soak. End-to-end latency was not measured. `AudioServer.get_output_latency()`, Chrome `baseLatency`, and `outputLatency` are estimates only. |
| Other environment | Desktop Firefox and WebKit | Firefox 159.0 was installed and not launched. Safari opened the threaded URL and produced no measurements. `safaridriver` needs an admin password. |
| Other environment | Devices, Linux, listening | Android Chrome, iOS Safari, and Linux RTSan (LLVM ≥ 20, including third-party allocation) were not run. `game-template/scenes/audio/procedural_music_audition.tscn` was not auditioned. Desktop emulation does not close the device gates. |

### Starting rules

- Read this file and `QUALIFICATION.md` before editing. Use the October 27 measurements as historical evidence.
- Inspect the `godot` and `game-template` working trees before editing. Preserve existing changes. Commit only when asked. In `game-template`, a requested commit goes on an `ai/<short-task-description>` branch, never directly on `main`.
- Follow `godot/AGENTS.md` and `game-template/AGENTS.md`. Activate `game-template/skills/godot-gdscript/SKILL.md` and `game-template/skills/godot-gdextension-cpp/SKILL.md` (Quick Reference, then a reference file only when the task needs it). Symphony is an engine C++ module under `modules/symphony/`. Apply the GDExtension skill’s ownership, audio-thread, and validation rules. Skip GDExtension project setup (`InitObject`, `.gdextension`, godot-cpp ABI). `audio-books`, `stk-code`, and `faust-code` are not required for N1–N5.
- Work in this order: **sanitizers → single-thread Web stall → browser workload and instrumentation → sustained qualification → platform and listening gates**.
- Preserve completed sections 2–3, queue and pool capacities, clip identities, offline-rate rules, and lifecycle protections unless a reproduced defect requires a targeted change.
- For each task, append an execution-log block: changed behavior, commands, binary/build identity, results, remaining failures, and the next unchecked task.
- Rebuild and re-run after a source change. A green historical log does not carry forward to a new binary.
- Native macOS builds use Apple clang. A shell that has sourced Emscripten puts `emcc`’s clang first, and `scons` then fails with an unknown target. Web builds need `source /Users/luong.pham/Work/emsdk/emsdk_env.sh` (`emcc` 6.0.12).

### Ordered execution tasks

#### N1 — Clear Symphony sanitizer failures first

- [x] **N1 — sanitizer batch, exit criteria passed 2026-10-28.** Known evidence: 417 UBSan reports from objects containing `alignas(64)` atomics; one Symphony TSan warning from the B0 mixer scaffold.

Helpers and the alignment that `memnew` cannot satisfy:

- `SymphonyPublishedU64` (`modules/symphony/core/symphony_published_u64.h`) — `alignas(64)` on `seq_`. Embedded at `AudioStreamPlaybackSymphony::frames_rendered`.
- `SymphonySeqLock` (`modules/symphony/core/symphony_seqlock.h`) — `alignas(64)` on `seq_` and `words_`. Embedded in `SymphonyVoicePool::metrics`, `SymphonyMusicScheduler::snapshot_lock`, `SpatialAcousticsEngine`, and `AudioEffectSymphonyReverb`.
- `SymphonySpscRing` (`modules/symphony/core/symphony_spsc_ring.h`) — `alignas(64)` on `write_pos` and `read_pos`. Embedded as `cmd_ring` and `audio_status` on `AudioStreamPlaybackSymphonyMusic`.

Actions:

- Give these embedded helpers the natural alignment of their `std::atomic<uint32_t>` members so a Godot `memnew` owner is a valid object. Keep the change inside Symphony. Leave Godot’s global allocator unchanged.
- Keep cache separation with explicit padding of at least 64 bytes between independently contended fields. Two fields at least 64 bytes apart cannot share one 64-byte cache line, whatever the object’s base address is.
- Preserve publication memory ordering, single-writer ownership, lock-free 32-bit atomics, and fixed capacities. `std::atomic<uint32_t>::is_always_lock_free` stays a hard requirement.
- Extend `tests/modules/test_symphony_web_publication.cpp`. The current cases construct the helpers on the stack. Add Godot-heap allocation of representative owners (`SymphonyVoicePool`, a music playback that owns both rings, and a playback that owns `SymphonyPublishedU64`), exercise publication and ring behavior under concurrent use, and construct and destroy those owners so UBSan sees the real `memnew` path.
- In `[Symphony][B0] Mixer-thread scaffold` (`tests/modules/test_symphony_b0.cpp`), stop calling `_test_transport_segment_count()` while the mixer thread is inside `mix_graph_rate`. Read `get_transport_snapshot()` (`tick`, `frame`, `generation`) and require `frame` to advance across the concurrent loop. Call `_test_transport_segment_count()` only from tests whose mixer is stopped.
- Leave the dummy-driver `mix_rate` warning attributed to `servers/audio/audio_driver_dummy.cpp`. Leave `[Symphony][Foundation][Audio] render_offline ignores a 44.1k device` in the suite. Leave sanitizer findings enabled: no suppression file, no removed check, no `halt_on_error` change that hides a report.

Exit criteria: the complete `*Symphony*` suite passes on the rebuilt binary. Symphony ASan reports are zero, Symphony UBSan reports are zero, and Symphony TSan races are zero. Any upstream report still prints its stack and the test that triggered it. Record the binary path, sanitizer log counts, and the next unchecked task (N2).

#### N2 — Reproduce and fix the single-thread Web stall

- [x] **N2 — single-thread Web stall, exit criteria passed 2026-10-28.** Known evidence: the single-thread page prints `tag=startup` and never prints `tag=after_1s` (`addons/symphony_audio/web_qualification_probe.gd`). Browser evaluation times out. The cause is unknown. A suspected cause stays a hypothesis until a stack or profile shows it.

Actions:

- Reproduce with Emscripten 6.0.12 and the existing qualification presets (`export_presets.cfg` features `symphony_qual_web_single` and `symphony_qual_web_threaded`; historical pages `http://127.0.0.1:8071/index.html` and `http://127.0.0.1:8072/index.html`). Serve with COOP `same-origin`, COEP `require-corp`, and CORP `cross-origin`. Headless Chrome has no WebGL2 and Godot then refuses to start; use a headed browser.
- Capture the browser console and a debugger stack or profile from the stalled page. Name the stuck frame.
- Compare three temporary diagnostic exports, and restore normal project behavior afterward: an engine-only empty scene, this project with its autoloads, and incremental Symphony playback. The shipped main scene and autoload list stay as they are once the comparison is done.
- From the captured stack, inspect main-thread loops, publication retries (`SymphonyPublishedU64::load` spins while `seq_` is odd), audio-driver callbacks, startup, and reentrant calls. Write the evidence next to any hypothesis. Apply the smallest fix at the layer that owns the stuck frame, and add a regression that fails on the reproduced trigger.

Exit criteria: threaded and single-thread pages stay responsive after startup and during playback. Audio unlock succeeds. Repeated start/stop and scene changes complete. The single-thread console contains `tag=after_1s`. Record the binary stamp, the console lines, and the next unchecked task (N3).

#### N3 — Run a real browser workload and expose bounded measurements

- [x] **N3.** Known evidence: `web_qualification_probe.gd` observes an empty main scene. Browser mix timing, underruns, active workloads, and resume backlog are unmeasured. `test/addons/symphony_audio/web_qualification_test.gd` reads `ProjectSettings.get_setting("audio/driver/output_latency")`, while the probe correctly uses `get_setting_with_override()`. Without the override the setting stays at the engine default, 15 ms.

Actions:

- Add a qualification scene that instances the section 4 Web baseline: 8 SFX voices, of which 4 are spatial, plus 8 synth notes, 1 stretched clip, and 1 reverb. Add the spectral variant as a separate scene or mode, not as a silent upgrade of the baseline.
- Activate that fixture only for the qualification exports (`symphony_qual_web_single`, `symphony_qual_web_threaded`). The ordinary editor and game boot keep their current main scene.
- Assert successful admissions, the expected active counts, and nonzero audio output. A silent page is a failed run.
- Collect callback duration, actual callback frame count, deadline misses, driver and worklet underruns, main-thread audio-update timing, preparation timing, memory, and rejected admissions.
- Add resume observations: expired-event catch-up, pending phrases, generation-map size, and queue occupancy.
- Store those in preallocated counters and bounded histograms or samples. Publish a report from the main thread infrequently. The audio callback does no logging, no allocation, and no per-block message.
- Keep the new metrics additive beside the existing debug metrics. Report Symphony mixing time separately from whole-driver rendering time.
- Change `web_qualification_test.gd` to read the latency request with `get_setting_with_override()`, matching the probe.
- When a counter cannot be read on a platform, print it as unavailable. A missing counter printed as zero is a failed report.

Exit criteria: the report names the active workload, the measurement boundaries, the sample rate, the actual callback sizes, the requested buffering, and the measured timing. Unsupported fields say unavailable. Record the report and the next unchecked task (N4).

#### N4 — Close soak, resume, and teardown gates

- [ ] **N4.** Known evidence: `tests/modules/test_symphony_profile.cpp` pushes every callback into `Vector<uint64_t> samples` and sorts it in `_percentile`. The 2026-10-27 web-spectral case stored 75,888,977 samples. Process RSS peaked at 646,000 KB while that vector was sorted, then fell after teardown. RSS is the harness vector plus the engine, not the Symphony budget. Both spectral cases failed only `CHECK(misses == 0)`.

Actions:

- Replace the duration-growing sample vector with bounded measurement storage. Keep P50, P95, and P99 in the test message, and print the bucket width or reservoir size and the resulting percentile resolution so a reader can tell a 43 µs result from a coarser bin.
- Investigate the native deadline spikes. Keep the tight-loop miss and the device underrun as separate numbers. Leave `CHECK(misses == 0)` in force until the spikes are understood and fixed. A quieter threshold is a plan change, not a silent edit.
- Capture a Symphony-byte baseline before each workload and again after complete teardown and reclamation. State which caches are intentionally retained. Run the workload twice in one process and show that the second peak matches the first.
- Run both native spectral cases for the full 1,800 seconds (`SYMPHONY_PROFILE_SECONDS=1800`, `--test-case='*Symphony*Profile*30*'`). Then run the browser baseline and the browser spectral workload for 1,800 seconds in both Web modes, using the N3 fixture and counters.
- Run ten 5-second background cycles, and separately suspend and resume the `AudioContext`. The 2026-10-27 threaded hide left `AudioContext` `running`; that result leaves the suspension gate open.
- Exercise unlock, pause/resume, scene changes, rapid transitions, and repeated start/stop on the qualification pages.

Exit criteria: P99 mixing is below half the actual deadline. Main-thread audio updates are below 2 ms P99. Foreground underruns are zero after warm-up. Memory and queues stay bounded. Teardown returns to the explained baseline. Resume does not replay an unbounded backlog. Record both native and both Web soaks, and the next unchecked task (N5).

#### N5 — Finish release coverage and evidence

- [ ] **N5.** Known evidence: `.github/workflows/web_builds.yml` builds two release templates (`threads=yes` and `threads=no`, Emscripten 6.0.12) and uploads artifacts. It does not launch a browser. Linux RTSan, real-device playback, loopback latency, and designer listening are not run.

Actions:

- Extend both existing Web build jobs with Chromium, Firefox, and WebKit playback checks that load the N3 fixture. Record the runner command and the artifact path in `QUALIFICATION.md` once that runner is stable. A `/tmp` script used while investigating stays investigation evidence.
- Add Linux RTSan coverage, including third-party allocation paths, on Linux LLVM ≥ 20. A macOS editor run does not close this gate. The earlier note lives in `MIGRATION.md`.
- Qualify real Android Chrome and iOS Safari devices. A desktop browser, a device emulator, and a user-agent override leave these gates not-run.
- Measure end-to-end latency through loopback. Keep `get_output_latency()`, `baseLatency`, and `outputLatency` in a separate “estimate” column.
- Listen on `game-template/scenes/audio/procedural_music_audition.tscn` for transitions, tails, spatial effects, clipping, and Web latency. Record who listened and what failed.
- Update `QUALIFICATION.md` with the date, binary identity, and an explicit pass, fail, or not-run for every gate in the table above. Leave production-readiness pending while any required gate is open.
- Update the engine/addon build pin only when the reviewed build artifacts for that pin exist. Invent no revision. Commit only when asked.

Exit criteria: `QUALIFICATION.md` shows a dated pass, fail, or not-run for every gate, and the still-open gates are listed. Record the next unchecked task, or write that N1–N5 are complete and production-readiness is still pending because a named gate remains open.

### Validation recipes

Native editor tests use Apple clang. From the engine root:

```bash
scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'
```

Symbolized ASan/UBSan and TSan both write `bin/godot.macos.editor.arm64.san`. Copy the ASan/UBSan binary aside before the TSan link. Read the sanitizer log even when the process exits 0. Doctest SUCCESS does not mean the sanitizer was clean. Historical options, which keep the process alive long enough to print every finding:

```bash
scons platform=macos target=editor arch=arm64 tests=yes use_asan=yes use_ubsan=yes debug_symbols=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
mkdir -p /tmp/symphony-qual
cp bin/godot.macos.editor.arm64.san /tmp/symphony-qual/godot.macos.editor.arm64.asan
ASAN_OPTIONS=halt_on_error=0:print_stacktrace=1:abort_on_error=0 \
UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1 \
  /tmp/symphony-qual/godot.macos.editor.arm64.asan --headless --test --test-case='*Symphony*'

scons platform=macos target=editor arch=arm64 tests=yes use_tsan=yes debug_symbols=yes module_raycast_enabled=no -j$(sysctl -n hw.ncpu)
TSAN_OPTIONS="halt_on_error=0 print_stacktrace=1 exitcode=0" \
  bin/godot.macos.editor.arm64.san --headless --test --test-case='*Symphony*'
```

Native release soak. Both spectral cases honor `SYMPHONY_PROFILE_SECONDS`. Use a shell whose `CC`/`CXX` are Apple clang:

```bash
SYMPHONY_PROFILE_SECONDS=1800 bin/godot.macos.template_release.arm64 --headless --test --test-case='*Symphony*Profile*30*'
```

Web templates, after `source /Users/luong.pham/Work/emsdk/emsdk_env.sh` (`emcc` 6.0.12). Confirm PFFFT on the object file (`pffft_simd_size` and the `SSE1` or `Scalar` string). `template_release` LTO drops that string from the linked wasm.

```bash
scons platform=web target=template_release threads=yes wasm_simd=yes
scons platform=web target=template_release threads=no wasm_simd=yes
scons platform=web target=template_release threads=no wasm_simd=no extra_suffix=scalar
```

Addon, from `game-template`. Run `gd check` on the changed addon scripts, then the Symphony addon suite:

```bash
GODOT_BIN=/path/to/bin/godot.macos.editor.arm64 node addons/agent_cli/gd.mjs check addons/symphony_audio test/addons/symphony_audio
GODOT_BIN=/path/to/bin/godot.macos.editor.arm64 node addons/agent_cli/gd.mjs test test/addons/symphony_audio
```

Designer listening is an editor session, not a headless test: open `scenes/audio/procedural_music_audition.tscn` and run it (F6). It uses `DemoScoreFactory` plus `MusicSystem.load_score` / `set_state` / `set_intensity` / `request_tempo` / `request_key`.

Stable browser-runner commands and their artifact locations belong in this section after those runners exist. Until then, a `/tmp` server or driver script is investigation evidence for the execution log, not a command to copy into the repository.

### Execution log

- Task: N1
- Changed behavior: `SymphonyPublishedU64`, `SymphonySeqLock`, `SymphonySpscRing`, and the RTPC parameter/analysis structs no longer require 64-byte allocation alignment. Contended fields are separated by 64 bytes of padding, so they cannot share a cache line at any base address. `memnew` owners stay within `max_align_t`. A temporary `SymphonyVoicePool` restores the singleton it replaced. The B0 mixer scaffold reads `get_transport_snapshot()` (`tick`, `frame`, `generation`) while the mixer runs and calls `_test_transport_segment_count()` only after the mixer has stopped. Signed 8-bit WAV samples widen by multiplying by 256. `RealFFT::resize(0)` returns before dividing by the size.
- Commands: unsanitized `scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no`, then `bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'`. Symbolized ASan/UBSan with the validation recipe, log `/tmp/symphony-qual/asan-ubsan2.log`. Symbolized TSan with the validation recipe, log `/tmp/symphony-qual/tsan.log`. Apple clang. Engine HEAD `2fd74b5002`, dirty tree.
- Binary / build identity: unsanitized `bin/godot.macos.editor.arm64` (125M, 2026-10-28 15:00, sha256 `89488b2e4fabc3cc26b32e31e1e0cfb0cc1ed1e44a576976ce429cdb8e46fcd4`). ASan/UBSan `/tmp/symphony-qual/godot.macos.editor.arm64.asan` (818M, 2026-10-28 15:38, sha256 `2397f0b7aae33058888fd305fb5616bc3171692668cfaee150665379aa89a342`). TSan `bin/godot.macos.editor.arm64.san` (239M, 2026-10-28 15:45, sha256 `f000ca38e773f2abcddcd3240dccc4359e9ec27095b7e14ca683ea1d1543b318`).
- Results: unsanitized 365/365 passed, 2,601,830 assertions. ASan/UBSan 365/365 passed, 2,601,829 assertions, exit 0. AddressSanitizer summaries: 0. UndefinedBehaviorSanitizer summaries: 0. TSan 365/365 passed, 2,601,829 assertions, exit 0. ThreadSanitizer reported 1 warning.
- Remaining failures: the one TSan warning is upstream. `[Symphony][Foundation][Audio] render_offline ignores a 44.1k device` calls `AudioDriverDummy::set_mix_rate` (`servers/audio/audio_driver_dummy.cpp:105`, also the restore at `test_symphony_foundation.cpp:546`) while `AudioDriverDummy::thread_func` reads `mix_rate` (`audio_driver_dummy.cpp:59`) before the driver lock. The test stays in the suite. No suppression file. Symphony ASan, UBSan, and TSan reports are zero.
- Next unchecked task: N2

- Task: N2
- Changed behavior: the no-thread `Semaphore::try_wait()` returns false when nothing was posted, so a drain loop finishes. `GLog` starts its flush worker only when `OS.has_feature("threads")`; otherwise `flush_now` and `stop_session` write the buffer on the caller. `Semaphore.wait()` is still a no-op without threads, and `flush_now` no longer waits on a worker that will never wake. The qualification probe, only under `symphony_qual_web_single` / `symphony_qual_web_threaded`, plays and stops a generator twice, reloads `node_2d.tscn`, and plays and stops again.
- Stuck frame: first idle `SceneTree::process`, inside `GLog._process` → `start_session`, at `while _flush_done_semaphore.try_wait()`. Evidence: headed Chrome on the 2026-10-27 nothreads export printed `tag=startup` and then spun at 100% CPU; `Runtime.evaluate` timed out. An instrumented copy showed no `FRAME_ENTER` and no `AUDIO_CB_ENTER`. Phase marks in `Main::iteration` printed `after_scene_physics`, `before_physics3d_step`, and `before_physics2d_step` for every physics step, then `before_idle_process`, and nothing after. A full project with only the probe autoload printed `tag=after_1s`. Empty engine, Jolt, 16 areas, and 16 `AudioStreamPlayer3D`s also stayed responsive. `SymphonyPublishedU64` / seqlock publication was not on this path. Those marks were removed after the frame was named.
- Commands: `scons platform=web target=template_release threads=no wasm_simd=yes` after `source /Users/luong.pham/Work/emsdk/emsdk_env.sh` (`emcc` 6.0.12). Headed Chrome, COOP/COEP/CORP, `http://127.0.0.1:8082/index.html` and `http://127.0.0.1:8083/index.html`. GLog suite: `node addons/agent_cli/gd.mjs test test/addons/GLog/glog_flush_now_test.gd` (8/8).
- Binary / build identity: single-thread wasm `bin/godot.web.template_release.wasm32.nothreads.wasm`, sha256 `26599c96f2cadb59a5df9602cff5e2b65e1d7cc7680554563dd93dc3961afa3c`, commit `2fd74b500244c722bb7166a9d894453459eafe06`, dirty, `threads: false`. Threaded page wasm was not rebuilt; console stamp `fb1205eaa` (2026-10-11). The threaded semaphore implementation did not change.
- Results: single-thread console printed `tag=startup`, `tag=after_1s` (`reported_output_latency_s=0.11466666311026`), `tag=playback_start_stop`, and `tag=after_scene_change`. `Runtime.evaluate` returned `ready=complete`. `AudioContext` state was `running` (`baseLatency` 0.005333, `outputLatency` 0.024). Threaded console printed the same four tags, `requested_latency_ms=50`, `after_1s` latency 0.072 s, and `AudioContext` `running`. Engine stamp on the single-thread page: `v4.8.dev.custom_build.2fd74b500`.
- Remaining failures: none for N2. Firefox, Safari, the ten-hide resume gate, the Web soak, and in-browser mix counts stay open.
- Next unchecked task: N3

- Task: N3
- Changed behavior: qualification exports instance the section 4 web baseline (8 SFX voices, 4 of them Early Reflections, 8 synth notes, 1 stretched clip, 1 Symphony reverb on bus `SymphonyQual`). `web_qualification_spectral.tscn` is a separate scene and is not selected by the current qualification features. The ordinary `run/main_scene` is unchanged. The audio callback records callback duration, callback size, deadline misses, output peak, and Symphony mix time in preallocated atomics and 64 log2-microsecond buckets. It does not log, allocate, or post a per-block message. The main thread publishes `SymphonyVoiceManager.get_qualification_report()`. `get_debug_metrics()` is unchanged. `driver_underruns` stays `unavailable` because there is no OS xrun counter. `worklet_underruns` is a real count only for AudioWorklet (threaded shared state index 4, or the no-thread posted counter); other drivers stay `unavailable`. Resume counters cover expired NoteOn/ClipOn catch-up, pending phrases, phrase-slot plus command-ring occupancy, active notes, and the controller generation-map size (capped at 8). A counter that has not been measured prints `unavailable`, including output peak before the first driver callback. `web_qualification_test.gd` reads the latency request with `get_setting_with_override()`.
- Commands: Apple clang `scons platform=macos target=editor arch=arm64 tests=yes module_raycast_enabled=no`, then `bin/godot.macos.editor.arm64 --headless --test --test-case='*Symphony*'`. Addon: `gd check` on the changed qualification scripts, then `gd test test/addons/symphony_audio/web_qualification_test.gd`. Web, after `source /Users/luong.pham/Work/emsdk/emsdk_env.sh` (`emcc` 6.0.12): `scons platform=web target=template_release threads=no wasm_simd=yes` and `threads=yes wasm_simd=yes`. Exported both qualification presets. Headed Chrome, COOP/COEP/CORP, `http://127.0.0.1:8084/index.html` and `http://127.0.0.1:8085/index.html`.
- Binary / build identity: editor `bin/godot.macos.editor.arm64` (125M, 2026-10-28 21:53, sha256 `1b3d801354a4b9b54b17f49d0a182a7c604382ce9147a9e5ef50ceade9662941`). Single-thread wasm `bin/godot.web.template_release.wasm32.nothreads.wasm`, sha256 `77d6232de6eb9056a51a113c0378879013cf465d08c0dc08a8e3f32ac21eab3a`. Threaded wasm `bin/godot.web.template_release.wasm32.wasm`, sha256 `5b938097964339c891423ee37634df34cb63ade164c6bdeb17abd76a13ab06b6`. Commit `2fd74b500244c722bb7166a9d894453459eafe06`, dirty.
- Results: native 366/366 passed, 2,601,841 assertions. Addon qualification suite 5/5. Both pages stayed responsive through startup, playback start/stop, and the scene change. `AudioContext` was `running` and `crossOriginIsolated` was true. Single-thread `after_scene_change`: `mode=single workload=web sample_rate=48000 callback_frames=128x304 requested_latency_ms=100 requested_without_override_ms=15 reported_output_latency_s=0.11466666311026 deadline_us=2666 n_driver=304 n_symphony=304 n_main_update=95 driver_p50_us=0 driver_p50_bucket=0-1 driver_p95_us=256 driver_p99_us=256 symphony_p50_us=0 symphony_p95_us=128 symphony_p99_us=256 main_update_p50_us=0 main_update_p99_us=4 driver_deadline_misses=0 symphony_deadline_misses=0 driver_underruns=unavailable worklet_underruns=0 prepare_us=6490 output_peak=2147481600 output=nonzero symphony_peak_milli=3652691 memory_used_bytes=434400 memory_peak_bytes=467264 rejected_admissions=0 expired_event_catchup=0 pending_phrases=0 generation_map_size=1 queue_occupancy=1 dialogue_queue=0 active_notes=8 admitted_sfx=8 admitted_spatial=4 admitted_notes=8 admitted_stretches=1 admitted_reverbs=1 phase_vocoders=0 spectral_gates=0`. Boundaries: `AudioDriver::audio_server_process;AudioStreamPlaybackSymphony::mix+AudioEffectSymphonyReverbInstance::process;AudioServer::update;qualification_prepare`. Histogram `log2_microseconds`, percentile `nearest_rank_bucket_low`. Threaded `after_scene_change`: `mode=threaded requested_latency_ms=50 callback_frames=128x196,256x58 deadline_us=2666 n_driver=254 n_symphony=254 n_main_update=105 driver_p50_us=4 driver_p50_bucket=4-8 driver_p95_us=256 driver_p99_us=512 symphony_p50_us=0 symphony_p95_us=256 symphony_p99_us=512 main_update_p50_us=4 main_update_p99_us=8 driver_deadline_misses=0 symphony_deadline_misses=0 driver_underruns=unavailable worklet_underruns=0 output=nonzero active_notes=8` with the same admissions, memory, and boundaries. `deadline_us` is the modal callback (128 frames). Each callback’s miss test uses that callback’s own frame count. Startup, before any driver callback, printed `unavailable` for sample rate, callback size, deadline, percentiles, and output peak rather than zero. `expired_event_catchup=0` and `worklet_underruns=0` are implemented counters. No admission or silence error was printed.
- Remaining failures: none for N3. The browser run is the web baseline. The spectral scene was admitted by the addon test and was not the browser workload. Firefox, Safari, the 1,800-second soaks, the hide/resume gate, Android, iOS, Linux RTSan, and listening stay open. A median in the `0-1` µs bucket is a measured bucket, not a missing sample; the web clock often does not advance inside a short callback.
- Next unchecked task: N4

## 1. Goals and operating limits

Fix all eleven review findings across the native module and addon while preserving procedural synthesis, adaptive music, recorded phrases, spatial acoustics, RTPCs, dialogue, and effects.

Support **threaded and single-threaded Web exports**, including desktop browsers and midrange mobile browsers. Feature availability remains consistent; concurrent capacity and buffering may differ.

Use these automatic memory defaults:

| Platform | Default Symphony budget |
|---|---:|
| Desktop native | 128 MiB |
| Mobile native | 64 MiB |
| Web, including mobile browsers | 32 MiB |

- Keep memory limits separate from voice and DSP limits. Increasing memory must not automatically increase callback workload.
- Keep Symphony mixing free of heap allocation, deallocation, blocking locks, compilation, decoding, FFT setup, and logging.
- Perform preparation and reclamation on the main thread. Single-threaded Web must work without background workers.
- Preserve fixed queue and pool capacities. Fix exhausted-capacity bugs through correct reuse rather than enlarging buffers.
- Reject operations that exceed capacity with explicit errors while preserving existing playback.

Implement in this order: regression fixtures → ownership and memory → scheduler → addon lifecycle → offline rendering → Web and release qualification.

## 2. Native ownership, memory, and scheduling

### PCM and stretch-context lifetime

- Give each prepared stretch context its own main-thread cache lease. Replacing a registry entry releases only the registry’s lease; submitted phrases retain their original PCM.
- New submissions use the replacement version. Existing prepared and active contexts finish using their retained version.
- Charge every unique retained version once. Replacement must reserve space before publishing; insufficient memory returns `ERR_OUT_OF_MEMORY` and leaves the previous registration intact.
- Transfer prepared entries into active ownership when claimed, removing their prepared-table records immediately.
- Remove every audio-side reference before publishing a context as returned. The audio thread must never inspect that context afterward.
- Reclaim leases and reset context fields only after the main thread observes the completed ownership transfer.
- Match clip commands using context generation and musical identity so stale commands cannot claim a recycled context.
- Preallocate scratch capacity for the supported sample-rate, tempo, and transpose ranges. Reuse it without growth during processing.

### Configurable memory budget

Add:

- Project setting `audio/symphony/memory_budget_mib`, where `0` selects the platform default.
- Main-thread `set_memory_budget_mib(value) -> Error` and `get_memory_budget_mib() -> int` on `SymphonyVoiceManager`, with `AudioManager` wrappers.
- `0` restores automatic selection; negative or overflowing values return `ERR_INVALID_PARAMETER`.
- Raising the limit changes admission headroom without allocating the allowance immediately. Lowering below committed usage returns `ERR_BUSY` without changing playback or the active limit.
- Document how a game settings menu can call this API and persist the preference. Do not add game-specific settings UI.

Expand accounting to include graph arenas, parked and recycled arenas, unique PCM versions, playback tables, stretch/FFT working storage, effect buffers, and preparation scratch. Reserve preparation peaks before allocation and release reservations on every failure path.

Use checked footprint calculations and capacity accounting for third-party buffers, verified against actual allocated capacities in debug tests. Report current usage, peak usage, retained PCM, reusable storage, preparation memory, and rejected admissions through existing debug metrics.

The budget covers Symphony-managed allocation payloads; Godot resources, browser audio buffers, allocator overhead, and total WASM memory remain separately reported.

### Scheduler and RTPC fixes

- Publish a phrase slot’s queued state **before** publishing its command. After publication, the producer relinquishes ownership until retirement.
- Parse, stamp generations, and sort phrases on the main thread. Audio adoption copies only valid events into preallocated storage; remove the large temporary phrase from the callback stack.
- Bound command consumption to the commands present at callback entry, capped by ring capacity.
- Compact consumed carry entries before admitting more note-offs. Retain the 512-entry simultaneous capacity and existing explicit overflow behavior.
- Make RTPC frame-count and coefficient caches exclusively audio-owned. Each callback takes one published parameter-count snapshot, detects new registrations, computes their coefficients, and processes that same snapshot.
- Bound controller generation bookkeeping and failed-submission retries so long sessions do not accumulate history indefinitely.

## 3. Addon behavior and offline rendering

### Music transitions

- Capture outgoing players before starting incoming playback; fade only those outgoing players.
- Preallocate a second synchronized player so layered states can crossfade correctly.
- Track transition generations and owned tweens. Invalidate old work on stop, reload, or a superseding transition; guard timer callbacks because `SceneTreeTimer` cannot be cancelled.
- Apply the same identity checks to delayed procedural-controller cleanup.
- When overlapping states cannot fit the memory budget, use fade-through-silence and wait for retirement without blocking. Report failure if the incoming state still cannot be prepared.
- Detach the stopped session’s BeatClock immediately. Its later cleanup must not alter a replacement session.

### Dialogue lifecycle

- Assign each accepted line and player activation an internal identity. Fade completion acts only on that activation.
- Emit completion exactly once for natural finish, interruption, and manual stop, including when another line is queued.
- Attach `play_from_dialogue_line()` completion callbacks to their accepted line rather than the next global completion signal.
- Track overlay completion and ducking across all owned voice players.
- Preserve the four-player pool. Bound queued lines to 32 by default, expose the queue limit, and report admission failure.
- Make `queue_line()` return whether the line was accepted; existing callers may ignore the return value.

### Offline rendering and content

- Add `AudioStreamSymphonyMusic.instantiate_offline_playback(seed = 0)`. Prepare its transport, instruments, and clips at **48 kHz before accepting phrases**.
- Keep live playback construction device-rate aware. `render_offline()` must refuse live-mode playbacks and must never retag an already prepared playback’s rate.
- Store canonical clip PCM at 48 kHz. Share rate-specific live views on demand, with every additional view included in memory admission.
- Update native tests, documentation, and the addon WAV helper to use the offline factory.
- Feed and render offline phrases incrementally rather than queuing the entire score. Write WAV exports in bounded chunks; reserve returned-array memory for direct `render_offline()` calls.
- Retune `rope_chain` through static output gain, initially reducing its final gain from `0.4` to `0.25`. Retain the existing peak and audibility assertions; avoid adding per-voice limiter cost.
- Repair stale authoring examples and regenerate their mirrors without weakening tests.

## 4. Web parity and performance qualification

Use Godot **Stream playback** throughout Symphony-owned players. Godot’s Sample path omits procedural generation and audio effects; Stream preserves those features with additional latency. [Godot Web audio documentation](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_web.html#audio-playback)

- Build matched release templates with `threads=yes` and `threads=no`, using the repository’s pinned Emscripten toolchain.
- Respect `wasm_simd` in Symphony and PFFFT build flags. Include a scalar build smoke test.
- Audit callback atomics for WASM support; replace aggregate atomics that introduce hidden locking with scalar publication or bounded snapshots.
- Use separate qualification export profiles: request **100 ms buffering for single-threaded Web** and **50 ms for threaded Web**. Report actual latency because browsers may round or add buffering.
- Test actual buffer lengths rather than assuming every callback contains 128 frames. [AudioWorklet processing guidance](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorkletProcessor/process)
- Load imported recording assets through Godot’s resource loader; reserve raw-file loading for external files.
- Test audio unlock, pause/resume, tab suspension, scene changes, and repeated start/stop. Resume must not replay an unbounded backlog.
- Keep every operator and subsystem available in both Web modes. Overload handling uses existing priority, LOD, virtualization, admission, and transition fallback policies.

Replace the sine-only performance fixture with mixed workloads:

| Qualification workload | SFX voices | Synth notes | Stretched clips | Spatial voices* | Reverbs |
|---|---:|---:|---:|---:|---:|
| Web/mobile baseline | 8 | 8 | 1 | 4 | 1 |
| Native desktop baseline | 32 | 32 | 2 | 8 | 2 |

\*Spatial voices are included in the SFX count. Exercise spectral operators in an additional workload.

Measure callback P50/P95/P99, deadline misses, underruns, main-thread preparation time, memory peaks, and admission failures. Initial gates are P99 mixing below 50% of the actual render deadline, steady main-thread audio updates below 2 ms P99 at 60 FPS, and no underruns during the controlled foreground baseline after warm-up. Larger workloads are capacity measurements, not assumed guarantees.

Use fixed counters and bounded sampling; avoid per-block messages or logs.

## 5. Regression tests, release gates, and delivery

Add deterministic regressions for:

- Clip replacement during preparation and playback, including budget rejection and final lease release.
- More than 1,000 sequential clip plays with continuing audible output.
- Context return racing immediate reuse, and forced phrase-publication interleavings.
- More than 10,000 phrase boundaries with correctly timed carried note-offs.
- Concurrent RTPC registration, smoothing changes, and callback-size changes.
- Single-stream and layered crossfades, rapid state requests, stop/reload, and stale fade completion.
- Queued and overlapping dialogue, with per-line completion exactly once.
- Identical seeded offline musical timing at 44.1/48 kHz device rates, plus long chunked WAV exports.
- Platform defaults, runtime budget increases, rejected decreases, allocation rollback, and teardown accounting.
- Existing preset level limits and corrected authoring examples.

Release gates:

- Rebuild and pass the complete native and addon suites.
- Run symbolized ASan/UBSan and TSan stress. Require zero Symphony memory errors and races; explicitly identify any remaining upstream reports.
- Add Linux RTSan coverage that includes third-party allocation paths, beyond Godot’s allocation counter.
- Extend both existing Web build jobs with browser playback tests. Automate desktop Chromium, Firefox, and WebKit checks; qualify real Android Chrome and iOS Safari on representative midrange devices.
- Run 30-minute native and Web mixed-workload soaks. Require bounded memory after warm-up and return to the expected baseline after teardown and reclamation.
- Perform listening checks for transitions, tails, spatial effects, clipping, and Web latency.

Deliver fixes as separately reviewable changes and update the engine/addon build pin together. `QUALIFICATION.md` now records the 2026-10-27 measurements. Production-readiness stays pending until the sanitizer, Web, sustained-performance, and listening gates pass.