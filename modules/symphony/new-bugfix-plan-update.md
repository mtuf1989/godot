# Add a detailed agent handoff to the Symphony bugfix plan

## Summary

Update only `modules/symphony/new-bugfix-plan.md`. Insert **“Next steps — agent execution handoff”** after the current status and preserved-invariants notes, before section 1.

Keep sections 1–5 and the historical measurements intact. The new section must distinguish completed implementation, known failures, unmeasured gates, and work requiring another environment. All new tasks start unchecked.

## Handoff structure and starting rules

Begin the new section with these instructions:

- Read this file and `QUALIFICATION.md`; use the October 27 measurements as historical evidence, not proof about newly rebuilt binaries.
- Inspect both repositories’ working trees before editing. Preserve existing changes and do not commit unless asked.
- Follow both repositories’ `AGENTS.md`. Required Godot skills are available under `game-template/skills/`. `audio-books`, `stk-code`, and `faust-code` are not required for N1–N5.
- Work in order: **sanitizers → single-thread Web stall → browser workload and instrumentation → sustained qualification → platform and listening gates**.
- Preserve completed sections 2–3, queue/pool capacities, clip identities, offline-rate rules, and lifecycle protections unless a reproduced defect requires a targeted change.
- For each task, record changed behavior, commands, binary/build identity, results, remaining failures, and the next unchecked task.

## Ordered execution tasks

### N1 — Clear Symphony sanitizer failures first

**Known evidence:** 417 UBSan reports from objects containing `alignas(64)` atomics; one Symphony TSan warning from the B0 mixer scaffold.

**Implementation instructions:**

- Inspect `SymphonyPublishedU64`, `SymphonySeqLock`, and `SymphonySpscRing`. Their 64-byte alignment propagates into owners allocated through Godot’s ordinary `memnew`.
- Use allocator-compatible natural alignment for these embedded helpers. Preserve cache separation through explicit padding where needed. Keep this change inside Symphony rather than changing Godot’s global allocator.
- Preserve publication memory ordering, single-writer ownership, lock-free 32-bit atomics, and fixed capacities.
- Extend the existing Web-publication tests to exercise Godot heap allocation of representative owners, plus publication and ring behavior under concurrent use. Include actual construction/destruction under UBSan.
- In the B0 concurrent scaffold, replace the raw transport-segment read with the published transport snapshot and verify that rendering advances. Restrict raw transport accessors to tests where the mixer is stopped.
- Keep the dummy-driver `mix_rate` warning separately attributed to upstream code. Do not suppress sanitizer findings or remove the 44.1 kHz offline regression.

**Exit criteria:** complete Symphony suite passes; zero Symphony ASan/UBSan reports and zero Symphony TSan races. Any upstream report retains its stack and triggering test.

### N2 — Reproduce and fix the single-thread Web stall

**Known evidence:** the single-thread page prints startup but never prints `after_1s`; browser evaluation times out. Its cause is unknown.

**Investigation instructions:**

- Reproduce with the pinned Emscripten toolchain and existing single/threaded qualification presets.
- Capture browser console output and a debugger stack/profile from the stalled page.
- Compare an engine-only empty scene, the project with autoloads, and incremental Symphony playback. Use temporary diagnostic configurations without changing normal project behavior.
- Check main-thread loops, publication retries, audio-driver callbacks, startup, and reentrant calls based on captured evidence. Do not label a suspected cause as established.
- Apply the smallest fix at the owning layer and retain a regression for the reproduced trigger.

**Exit criteria:** both modes remain responsive after startup and during playback; audio unlock succeeds; repeated start/stop and scene changes complete; the single-thread `after_1s` marker appears.

### N3 — Run a real browser workload and expose bounded measurements

**Known evidence:** the existing qualification probe observes an empty main scene. Browser mix timing, underruns, active workloads, and resume backlog are unmeasured.

**Implementation instructions:**

- Extend the qualification probe with a dedicated qualification scene using the section 4 Web baseline: 8 SFX, including 4 spatial voices, 8 synth notes, 1 stretched clip, and 1 reverb. Add the spectral variant separately.
- Activate this fixture only for qualification exports. Verify successful admissions, active counts, and nonzero audio output.
- Collect callback duration and actual frame count, deadline misses, driver/worklet underruns, main-thread audio-update timing, preparation timing, memory, and rejected admissions.
- Add resume observations for expired-event catch-up, pending phrases, generation-map size, and queue occupancy.
- Use preallocated counters, bounded histograms/sampling, and infrequent main-thread reports. No callback logging, allocations, or per-block messages.
- Keep debug metrics additive. Separate Symphony mixing time from whole-driver rendering time.
- Correct the existing qualification test to read latency settings with `get_setting_with_override()`, matching the probe.

**Exit criteria:** reports identify the active workload, measurement boundaries, sample rate, actual callback sizes, requested buffering, and measured timing. Unsupported measurements remain explicitly unavailable rather than reporting zero.

### N4 — Close soak, resume, and teardown gates

- Replace the native profile harness’s duration-growing sample vector with bounded measurement storage. Preserve meaningful P50/P95/P99 reporting and document its resolution.
- Investigate native deadline spikes without equating tight-loop wall-clock misses with device underruns. Keep the existing zero-miss failure visible until resolved; do not silently relax the assertion.
- Capture a baseline before each workload and after complete teardown/reclamation. Explain intentionally retained caches and verify repeated workloads do not accumulate memory.
- Run both native spectral cases for the full 1,800 seconds, then run the actual browser baseline and spectral workloads for 1,800 seconds in both Web modes.
- Run ten 5-second background cycles and explicit `AudioContext` suspend/resume cycles. A visibility change while audio remains running does not satisfy the suspension test.
- Exercise unlock, pause/resume, scene changes, rapid transitions, and repeated start/stop.

**Exit criteria:** P99 mixing below half the actual deadline; main-thread audio updates below 2 ms P99; zero foreground underruns after warm-up; bounded memory and queues; expected teardown baseline; no unbounded replay on resume.

### N5 — Finish release coverage and evidence

- Extend both existing Web build jobs with Chromium, Firefox, and WebKit playback checks using the real fixture.
- Add Linux RTSan coverage, including third-party allocation paths.
- Qualify real Android Chrome and iOS Safari devices; desktop emulation does not close these gates.
- Measure end-to-end latency through loopback. Keep browser latency estimates separately labeled.
- Perform designer listening for transitions, tails, spatial effects, clipping, and Web latency.
- Update `QUALIFICATION.md` with dated evidence and explicit pass/fail/not-run states. Keep production readiness pending while required gates remain open.
- Coordinate engine/addon build pins only when the corresponding reviewed build artifacts exist; do not invent a revision or commit automatically.

## Validation recipes to include

Copy the existing native, addon, Web, and soak commands from `QUALIFICATION.md`, adding:

- Separate symbolized ASan/UBSan and TSan build/run recipes.
- A warning that sanitizer builds share the `.san` filename and must be copied aside before relinking.
- An instruction to inspect sanitizer logs even when the process exits successfully.
- Apple clang for native builds; Emscripten 6.0.12 for Web builds.
- `gd check` for changed addon scripts before the complete Symphony addon suite.
- Stable browser-runner commands and artifact locations once those runners exist; temporary `/tmp` scripts are investigation evidence, not permanent automation.

## Documentation acceptance and assumptions

- Only the requested Markdown file changes during this documentation task.
- Every execution task includes evidence, concrete actions, and an observable completion gate.
- The handoff explicitly identifies **N1 as the next implementation batch**.
- Preserve historical failures and avoid claiming any new validation occurred.
- Run `git diff --check` and inspect the final documentation diff. No engine rebuild is necessary for this documentation-only update.
- The file has not been edited in this turn because Plan mode prohibits repository mutations.
