# Procedural music v1 — Hybrid, responsive game scores

> **Part of one combined plan with [`tech-fix-plan.md`](tech-fix-plan.md).**
> - The order of every music milestone relative to the technical phases (P0–P10) is set in that file's **§Unified roadmap**: S0 Foundation → S1 Transport ‖ DSP fixes → S2 Instruments → S3 Recordings + composer → S4 Adaptive → S5 Qualification.
> - Shared prerequisites are specified there and are not repeated here:
>   - lifetime/RT safety (P1);
>   - Float→Audio promotion order, the SeqLock fix, integer seeds, the frame counter and `render_offline` (P1b);
>   - the sample-rate policy (P4);
>   - the resettable-state contract and ModalBank `pitch_ratio` (P10);
>   - music-safe voice budgeting (P8a).
>
> **Status (2026-10-09):** tech **P0–P8b** and **P10** done; music **N0–N3**, **G1–G2**, **MusicStream**, and **V1** automated qualification done. Optional **P9** + listening deferred — see `QUALIFICATION.md`.

## 1. Goal and scope

Build a system that combines generated notes with recorded musical phrases, responding coherently to exploration, tension, and combat.

Symphony already provides synthesis and adaptive playback. This feature adds the missing musical timeline, note sequencing, composition rules, and coordinated playback of generated and recorded material.

The first release will support:

- **Flexible authoring per piece:** designers can supply progression and motif palettes, partially authored tracks, or complete phrases. Generation fills unlocked regions.
- **Hybrid playback:** Symphony instrument graphs and recorded phrases share one sample-accurate timeline.
- **Adaptive arrangement:** energy layers react on the next beat; harmony, motifs, recorded phrases, and key changes wait for compatible phrase boundaries.
- **Live recording adaptation:** tempo from **80% to 125% of source tempo**, and transposition within **±6 semitones**. Prefer an authored variant outside that envelope.
- **Desktop first:** validate on the existing macOS environment.
- **Text-based authoring:** `.tres` resources, validation, an audition scene, and offline rendering.

Initial exclusions: a piano-roll editor, unrestricted composition, AI music generation, automatic harmonic analysis of recordings, long-track streaming, save-state restoration, and mobile/web qualification.

## 2. Architecture and interfaces

### Ownership across the two repositories

| Component | Owner | Responsibility |
|---|---|---|
| Musical transport and scheduler | Symphony C++ | Convert musical time to sample positions; schedule events and automation |
| Instrument voice pools | Symphony C++ | Render notes, envelopes, releases, and bounded polyphony |
| Recorded phrase renderer | Symphony C++ | Resident PCM playback, stretching, transposition, latency alignment |
| Score resources and composer | Game Audio Layer | Resolve authored material, choose harmony, generate notes, select compatible recordings |
| Adaptive controller | Game Audio Layer | Interpret state/intensity, enforce pacing, prepare future phrases |
| Existing `MusicSystem` | Game Audio Layer | Public facade, playback mode selection, Music bus integration, signals |

The dependency remains **Game Audio Layer → Symphony**. C++ receives prepared musical events; it does not interpret combat or exploration.

Each procedural score uses one `AudioStreamPlayer` on the existing Music bus. Its native playback owns the transport and all internal voices. Individual notes do not create players or pass through `AudioManager.play_event()`.

### Authoring resources

Add typed resources under `addons/symphony_audio/`. Keep configuration immutable during playback and store runtime state in a separate controller.

| Resource group | Required information |
|---|---|
| **Score** | Stable ID, schema/generator version, initial tempo/key/meter, sections, tracks, limits |
| **Section** | Phrase length, progression choices, intensity profiles, allowed transitions, dwell/rest policy |
| **Track** | Stable ID, instrument or recording pool, musical role, pitch range, polyphony, gain, protection priority |
| **Harmony palette** | Explicit scales and chord progressions with durations, weights, and cadence labels |
| **Motif/rhythm palette** | Relative-pitch motifs, rhythmic patterns, permitted variations, dynamics and groove |
| **Authored phrase** | Fixed notes, locked regions/tracks, harmonic anchors, optional transition fills |
| **Recorded phrase entry** | Source tempo/key/meter, harmonic timeline, body/lead/tail markers, entry/exit tags, weight, repeat policy |

Important authoring rules:

- Authored and locked material takes precedence over generated material.
- An unlocked region requires a suitable palette or explicit fallback.
- Fully authored scores remain valid without generation palettes.
- Notes declare either absolute pitch or relative pitch. Key changes resolve relative notes; absolute locks can constrain or prevent a key change.
- Recording metadata is authored explicitly. Pitch shifting changes its root, not its chord qualities or progression.
- Conflicting locked harmony and recorded harmony produce validation errors rather than automatic rewriting.

### Native additions

Add:

- `AudioStreamSymphonyMusic`: generic music stream configuration and preparation.
- `AudioStreamPlaybackSymphonyMusic`: transport, event execution, note pools, and clip rendering.
- `SymphonyMusicPhrase`: main-thread builder, copied into immutable native event storage on submission.
- `SymphonyMusicClip`: file, source tempo/meter, and sample markers.
- `MusicNoteInput`: DSP operator exposing frequency, velocity, sample-playback pitch ratio, note-on, note-off, and gate.

Minimum playback control surface:

```gdscript
# Main-thread calls; native code validates and publishes bounded commands.
submit_phrase(phrase, start_tick: int, generation: int) -> Error
replace_future(from_tick: int, generation: int) -> Error
schedule_tempo(bpm: float, start_tick: int, ramp_ticks: int) -> Error
schedule_track_gain(track_id: int, gain: float,
                    start_tick: int, fade_ticks: int) -> Error
get_transport_snapshot() -> Dictionary
drain_status() -> Array[Dictionary]
```

Phrase builders accept note events containing track, start tick, duration, MIDI pitch, velocity, and stable note ID. Clip events contain track, start tick, clip ID, and transposition.

Extend `MusicSystem` additively:

```gdscript
load_score(score: ProceduralMusicScore, seed: int) -> Error
set_state(state_name: StringName) -> void
set_intensity(value: float) -> void
request_tempo(bpm: float) -> Error
request_key(root_pitch_class: int, scale_id: StringName) -> Error
```

Add a `PROCEDURAL` playback mode and delegate its work to a dedicated `ProceduralMusicController`. Preserve existing `load_graph()` behavior and adaptive stream modes.

Provide an isolated offline-render helper accepting a score, seed, duration, timed controls, and WAV destination. It uses the same composer and native rendering kernel without disturbing live playback.

## 3. Playback and composition behavior

### A. One musical transport

- Store musical positions as **64-bit ticks at 960 ticks per quarter note**.
- Use an audio-owned rendered-frame counter as the timing authority.
- Maintain continuous, bidirectional tick/sample mapping across tempo changes. Changing BPM must not rescale elapsed history.
- Support tempo steps and ramps. Ramps interpolate BPM over musical ticks; use analytic mapping and fractional sample accumulation to avoid drift.
- Apply meter changes at phrase boundaries with a new bar origin. Support 4/4, 3/4, and 6/8 in qualification tests.
- **Sample-rate policy D** (tech plan P4, `core/symphony_rate_policy.h`):
  - compile instrument graphs and prepare PCM at the device rate **only when it is 44.1 kHz or 48 kHz**; otherwise run at 48 kHz and let Godot resample;
  - offline and golden renders always use 48 kHz;
  - a driver restart that changes the rate requires stop and reload;
  - instrument graphs must pass the 44.1k/48k equivalence test (±1 cent, ±0.5 dB, ±1 ms).
- Split rendering at event boundaries, then execute graph spans no larger than the existing 32/64-sample micro-block.
- At equal timestamps, process note-offs before note-ons, then use a stable total ordering.

`BeatClock` observes this transport in procedural mode. Main-thread beat/bar signals are notifications; they do not trigger musical events. Repair legacy tempo continuity and crossed-beat handling without moving procedural scheduling back into `_process()`.

The legacy BeatClock repair and sample-accurate `trigger_at` for SFX graphs are specified in tech plan **P7 (items R and N)** and implemented in this milestone (N1):
- incremental beat accumulator;
- explicit loop-wrap handling, so bar 0 fires on every loop;
- emitting every crossed beat (capped);
- the `fire_ahead_s` + target-time signal args;
- the `AudioStreamPlaybackInteractive::get_playback_position()` fix.

Both the transport and `trigger_at` use the P1b `frames_rendered` counter as their single timing authority.

Pause freezes musical position. Resume continues without replaying elapsed events. Initial playback starts at tick zero; arbitrary seeking remains outside v1.

### B. Note-aware instrument playback

Use precompiled graph pools, with one graph instance per available instrument voice.

- Require one `MusicNoteInput` per music instrument graph.
- Update pitch, velocity, seed, and gate together at the event boundary.
- Connect sample players to note-on; connect envelopes to gate. Note-off must not restart a sample.
- Bypass automatic smoothing for discrete note pitch and velocity. Explicit glide remains an authored graph behavior.
- Move Float→Audio promotion to follow its producing operator, so consumers receive current-span control values rather than the previous block’s values. *(Delivered in S0 by tech plan P1b.)*
- Use the bounded `reset_for_note(seed)` contract from tech plan **P10**:
  - **how it works:** each operator's fresh `export_state` is captured at construction; reset is `import_state(pristine)`, plus clearing the used region of large buffers, plus a reseed from `core/symphony_seed.h`. Never reset graphs by copying whole-object memory.
  - **resettable flag:** operators are opted in with a `resettable` descriptor flag, and a generic conformance test proves reset ≡ fresh construction for every flagged operator.
- Initially approve oscillator/FM/noise, envelopes, filters, math, waveshaping/saturation (after the tech plan P3 anti-aliasing fixes), **ModalBank** (mallets, bells, plucks), and resident sample-player graphs. Reject graphs containing operators that are not flagged `resettable`.
- **ModalBank instruments.** Drive pitch through the P10 `pitch_ratio` pin: the preset declares `root_hz`, and the note sets `ratio = note_hz / root_hz`. Coefficients are recomputed at the note boundary. Release completion is detected through P1's per-block energy flush, which lets ringing voices reach exact silence and be reclaimed.
- Place large reverbs and delays on the Music bus for the initial presets.
- Retain releases after note-off and phrase replacement. Each active voice owns its release deadline and note identity.

Reserve capacity for protected bass and percussion tracks. Validate overlap, including release tails, before accepting a phrase. If runtime capacity is exhausted, preserve active protected notes and drop the lowest-priority incoming optional note, with a diagnostic.

Global voice budgeting counts the score’s aggregate processing cost. It must not independently steal its internal note voices.

Per tech plan **P8a** (lands before G2), the music playback registers as a **VITAL** voice with the CPU manager:
- the manager may request an LOD demotion but never steals it;
- the score reports its aggregate cost through the same per-voice cost-EWMA stats that P1 introduces.

### C. Recorded phrase rendering

Vendor **Signalsmith Stretch 1.3.2**, pin its exact revision and compatible Signalsmith Linear dependency, and retain license notices. Start with stereo float processing and the default preset with split computation enabled. The upstream implementation exposes latency queries and separates configuration from processing; Symphony must still verify its real-time behavior. [Upstream source](https://raw.githubusercontent.com/Signalsmith-Audio/signalsmith-stretch/main/signalsmith-stretch.h)

Implementation policy:

- Accept short WAV phrase assets initially; decode them (any `AudioStreamWAV` format, using the decode path from tech plan P2 N3) and resample them to the music rate chosen by policy D before playback, into resident stereo PCM.
- Add a stereo music cache rather than changing the existing mono `SharedPCMCache` contract.
- Allocate, configure, reset, and prime processor contexts off the audio thread.
- Give each context exclusive ownership while active. Return it for preparation only after audio acknowledgment.
- Use fractional source-frame accumulation to maintain the correct average tempo ratio.
- Account separately for source lookahead and output latency.
- Give all score tracks a common rendering latency. Delay generated notes and bypass paths as needed to align them with processed recordings.
- Report the emitted musical position to observers; apply device-output latency only when estimating listener position.
- Handle leads and tails using authored markers. Crossfade only across compatible harmony.
- At incompatible key/harmony boundaries, finish or fade incompatible pitched tails before the new phrase; preserve compatible common tones and unpitched tails.

Tempo changes normally begin at the next bar and ramp over one bar. If the ramp would exceed an active clip’s supported envelope, defer to a phrase boundary and choose a compatible variant. When no valid variant exists, retain the current valid musical setting and report the reason.

A mode or progression change requires compatible recorded material. Transposition alone cannot satisfy it.

### D. Bounded composition pipeline

Compose one phrase ahead on the main thread. Keep the current and next phrase prepared; initial phrase length is four bars, with authored two- and eight-bar phrases supported.

Run these stages in order:

1. **Resolve constraints:** collect locked notes, fixed regions, harmonic anchors, track ranges, and recording requirements.
2. **Choose harmony:** select an authored progression compatible with those constraints and the target section.
3. **Choose recordings:** filter by harmonic timeline, mode, phrase length, entry/exit tags, and transform envelope; then rank by weight and repetition history.
4. **Generate tracks:** derive bass, chord voicings, melody, arpeggios, and percussion from shared harmony and the track’s palettes.
5. **Apply permitted variation:** rhythm selection, voicing/inversion, limited motif variation, dynamics, and groove.
6. **Validate and publish:** check timing, pitch ranges, locked-material preservation, harmony, capacity, and clip compatibility; seal and submit atomically.

Initial generation behavior:

- Bass emphasizes roots/fifths, with passing tones permitted by the rhythm palette.
- Chord tracks use bounded candidate voicings and an eight-candidate beam for voice leading.
- Melody starts from authored motifs; cadence notes follow the selected harmonic context.
- Percussion selects authored rhythmic patterns by energy profile.
- Swing and humanization affect generated events only, unless authored material explicitly opts in.
- Preserve motif identity by limiting variation per phrase.

Use candidate-count limits, not wall-clock timeouts, to keep generation repeatable. Hard constraints are enforced by construction or validation; voice-leading and novelty are ranking heuristics.

Derive independent integer RNG seeds from:

`root seed + score ID + section ID + phrase index + track ID + stage ID`

Do not use float seed parameters, resource addresses, creation order, or one shared RNG stream. Extend approved stochastic DSP nodes with independently derived note/node seeds.

Tech plan **P1b (item G)** delivers this in S0:
- `core/symphony_seed.h` provides `splitmix64` and `symphony_node_seed(voice_seed, node_id, authored, deterministic)`;
- a scoped compile context gives operators their node ID at creation;
- Noise, GrainCloud and StochasticTrigger take integer seeds.

The composer's stage seeds and the note-voice reseed in `reset_for_note` both use the same helper.

The reproducibility contract is identical symbolic plans for the same score/version, seed, and admitted controls at musical ticks. Cross-platform bit-identical audio is not required.

### E. Adaptive behavior and failure policies

- **Next beat:** change layer gains and permitted timbral controls, with a one-beat gain fade.
- **Phrase boundary:** change harmony, motif family, recorded phrase, and key.
- **State churn:** coalesce pending requests to the latest target; do not interrupt a transition already being rendered.
- **Pacing:** sections author dwell time, cooldown, and optional rest windows. Explicit combat entry bypasses dwell; departure follows the section’s release policy.
- **Emergency stop:** fade immediately through the native renderer.

All queues, prepared phrases, and voice pools have fixed limits. Initial defaults:

| Limit | Default |
|---|---:|
| Musical tracks | 8 |
| Synth note voices, including releases | 32 |
| Simultaneous recorded lanes | 4 |
| Stretch contexts, including incoming/outgoing contexts | 8 |
| Events per phrase | 4,096 |

Charge graphs, PCM, processor storage, and queues against Symphony’s existing memory budget. Reject preparation clearly when it does not fit.

Failure behavior:

- Publish complete phrases atomically; reject queue overflow without partially publishing events.
- Never put essential note-offs in a queue where overflow could discard them.
- Defer late musical changes to the first safe boundary.
- If generation misses its deadline, repeat the current validated phrase, maintaining its harmonic context and tempo.
- Log fallback iterations and actual admitted control ticks for replay.
- Invalid locked content is an authoring error; generation does not repair it silently.
- Coalesce noncritical status notifications. Lifetime acknowledgments use separate ownership state and cannot be dropped.

### F. Real-time safety

Before expanding playback (all delivered in **S0** by tech plan P1/P1b):

- ✅ Remove the blocking mutex from Symphony’s audio-thread voice-budget traversal. *(P1 `try_lock`.)*
- ✅ Retain playback/package ownership until audio readers have finished and acknowledged removal. *(P1 single-owner hand-off: the mixing thread owns package pointers while the voice is reachable; main reclaims only when the refcount proves the voice is unreachable.)*
- ✅ Destroy graphs, PCM, resources, and processor contexts off the audio thread. *(P1 invariant I5: free only on main.)*
- Use bounded SPSC commands and coherent status snapshots.
- For transport snapshots, use the P1b-fixed SeqLock (`core/symphony_seqlock.h`), which uses atomic payload words with correct fences and is TSan-clean, or a double-buffered snapshot. Never use a plain-`memcpy` payload. *(P1b — pending.)*
- Keep resource reads, dictionaries, signals, compilation, decoding, and logging outside rendering.

## 4. Implementation milestones

| Milestone | Stage | Repository | Prerequisites (tech plan) | Deliverable and exit gate |
|---|---|---|---|---|
| **N0 — Safety foundation** | S0 | Godot | **= tech P0 ✅ + P1 ✅ + P1b ✅** (no extra work) | Establish regression baselines; fix voice lifetime/locking and Float→Audio control ordering. Focused concurrency and control-timing tests pass. |
| **N1 — Transport and scheduler** | S1 | Godot | S0; absorbs the tech P7 R + BeatClock specs; uses the P4 rate policy | **Done:** `symphony_music_transport.h` / `symphony_music_scheduler.h`; BeatClock rewrite; `trigger_at`; Interactive clip position; `test_symphony_music_n1.cpp`. Stream wrapper landed as MusicStream (below). |
| **N2 — Musical instruments** | S2 | Godot | **P10** (reset contract, ModalBank `pitch_ratio`), **P3** (waveshaper/saturator fixes) | **Done:** `MusicNoteInput`; `symphony_note_pool.h` (resettable validation, seeded voices, release deadlines, protected capacity); `test_symphony_music_n2.cpp` (multi-track phrase + 44.1/48k). Stream wrapper deferred to G2. |
| **N3 — Recorded phrases** | S3 | Godot | P2 N3 (WAV decode path) | **Done:** vendored Signalsmith Stretch **1.3.0** (+ Linear **0.1.2**; plan cited 1.3.2 — tag absent upstream); `SymphonyMusicStereoCache`; stretch pool ownership; tempo/transpose envelope; common-latency hybrid synth+clip test (`test_symphony_music_n3.cpp`). |
| **G1 — Score authoring and composer** | S3 | Game template | P1b G (seed helper) | **Done:** `addons/symphony_audio/procedural/*` resources; `ProceduralMusicComposer` stages; `ProceduralMusicLedger`; `procedural_composer_test.gd` (7 cases). |
| **MusicStream — Native playback** | S4 prep | Godot | N1–N3, P8a, P10 | **Done:** `AudioStreamSymphonyMusic` / `AudioStreamPlaybackSymphonyMusic`; phrase submit, pools, stretch, VITAL; `test_symphony_music_stream.cpp`. |
| **G2 — Adaptive integration** | S4 | Game template | MusicStream + **P8a** | **Done:** `ProceduralMusicController`; MusicSystem `PROCEDURAL` + `load_score`/`set_intensity`/`request_tempo`/`request_key`; phrase-ahead + fallback; `procedural_music_g2_test.gd`. |
| **V1 — Qualification and documentation** | S5 | Both | all phases (P9 optional) | **Done (automated):** `test_symphony_music_v1.cpp`; `DemoScoreFactory`; 1000-seed batch; `QUALIFICATION.md` + API docs. **Deferred with P8b/P9:** 30‑min release profiling, RTSan CI, designer listening. |

Follow the stage order in `tech-fix-plan.md` §Unified roadmap. Within it, implement native timing and a fixed hybrid playback example before adding composition heuristics. Keep native and Game Audio Layer changes in separate reviewable commits.

The demonstration score should contain generated bass, chord accompaniment, melody/percussion, and a recorded phrase layer. Include palette-driven, partially locked, and fully authored examples.

## 5. Verification and completion criteria

### Automated correctness

- Note events at offsets 0, 1, 31, 32, 63, and callback boundaries execute within one output sample of their scheduled timestamp.
- Pitch and gate change together; no stale first-block pitch or unintended smoothing.
- Note-off does not restart sample playback.
- Tempo steps/ramps preserve position and exhibit no cumulative scheduling drift beyond one sample.
- Meter changes preserve bar origins.
- Different callback sizes preserve the symbolic event sequence.
- Main-thread hitches leave audio scheduling intact; longer starvation invokes the documented phrase fallback.
- Phrase replacement preserves releases and cannot apply an old note-off to a reused voice.
- Pause, stop, rapid reload, scene teardown, and clip-context reuse have safe ownership.
- Locked material remains unchanged across regeneration.
- Batch-test at least 1,000 seeds against timing, range, harmony, capacity, and reproducibility invariants.
- Recording selection rejects incompatible progression/mode combinations and unsupported transforms.

### Audio and integration

- Render fixtures at unity, both tempo-envelope endpoints, ±6 semitones, and combined transformations.
- Check duration, pitch, stereo preservation, finite output, loop joins, leads/tails, and measured latency.
- Audition drum, sustained, and mixed instrumental recordings for transient smearing, clicks, flam, and key clashes.
- Verify Music bus effects and dialogue ducking.
- Run existing adaptive playback and Symphony preset regressions.
- Confirm musical quality with designer listening; automated constraints alone do not establish that the score sounds good.

### Real-time and performance

- Zero allocation/free, blocking locks, file I/O, or resource destruction in Symphony’s music rendering path, including steady-state stretching and transitions.
- Run focused ASan/TSan and real-time instrumentation for publication, retirement, and context ownership.
- Profile release builds at 44.1 and 48 kHz, across supported callback sizes.
- Record callback P50/P95/P99, memory, phrase preparation time, context count, fallback count, and dropped optional notes.
- Proposed initial performance gate: the maximum configured demonstration workload uses less than 50% of a 512-frame callback at P99 on the reference Mac, with no feature-caused deadline misses during a 30-minute run. This is a target to measure, not an established capacity.

The feature is complete when a seed-driven hybrid score responds using the chosen beat/phrase policy, preserves authored intent, adapts recordings within the agreed envelope, survives generation stalls and teardown, and passes timing, real-time, regression, performance, and listening gates.

Evidence for this plan comes from local source inspection, the supplied talk notes, and upstream backend documentation. New playback benchmarks and listening tests have not yet been performed; the `audio-books`, STK, and Faust knowledge tools were unavailable.
