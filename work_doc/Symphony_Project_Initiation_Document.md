# Symphony — Project Initiation Document

## 1. Project Overview

### 1.1 What Is Symphony?

Symphony is a C++ module for the Godot Engine that implements a real-time, block-processed, node-based DSP (Digital Signal Processing) flow graph. It is inspired by Unreal Engine's MetaSound architecture but is not a direct copy. Symphony brings procedural audio generation, real-time synthesis, and sample-accurate event scheduling to Godot — capabilities that currently do not exist in the engine.

### 1.2 Why Build This?

Godot's current audio system is designed for playback of pre-rendered assets with basic bus effects. It lacks:

- Audio-rate parameter modulation (parameters update at game tick rate ~30-60 Hz, not audio rate ~48,000 Hz)
- Sample-accurate event triggering (events are quantized to the next audio block boundary)
- A node-based procedural audio authoring workflow
- Runtime DSP graph construction and modification

Symphony fills this gap by providing a self-contained DSP execution engine that runs inside Godot's existing audio pipeline without modifying core engine code.

### 1.3 Architectural Approach

- **Implementation:** Godot C++ Module located at `modules/symphony/`
- **Core Engine Modifications:** Zero for v1. The module operates entirely within the existing `AudioStreamPlayback::mix()` interface.
- **Integration Point:** Symphony graphs are exposed as `AudioStreamSymphony` resources, playable through standard `AudioStreamPlayer` nodes.
- **Editor:** Native `EditorPlugin` using Godot's `GraphEdit` widget for visual graph authoring.

---

## 2. Architectural Foundation

### 2.1 How It Fits Into Godot's Audio Pipeline

```
AudioDriver::thread_func()
  → AudioDriver::lock()                    // Existing driver mutex (we don't touch this)
  → AudioServer::_driver_process()
    → AudioServer::_mix_step()
      → playback_list iteration
        → AudioStreamPlaybackSymphony::mix()   // ← OUR ENTRY POINT
          → Internal 64-sample micro-block loop
            → Topological DAG evaluation
            → Per-operator Execute() calls
            → FTrigger sample-accurate scheduling
          → Output written to Godot's AudioFrame* buffer
      → Bus effect processing (standard Godot)
      → Bus send routing (standard Godot)
  → AudioDriver::unlock()
```

### 2.2 Key Architectural Decisions

| Decision | Rationale |
|----------|-----------|
| Zero core engine patches in v1 | Eliminates merge conflicts with upstream Godot. Forces self-contained design. Allows trivial rebasing. |
| Internal 64-sample micro-block subdivision | Achieves sub-block granularity without modifying `_mix_step()`. Standard pattern used by FMOD, Wwise, and VST plugins. |
| Accept the driver mutex | The mutex only contends during bus reconfiguration (rare, bounded). Our DAG runs inside `_mix_step()` which is already inside the lock. No additional contention. |
| Module (not GDExtension) | Full access to editor infrastructure (`GraphEdit`, `EditorPlugin`, inspector integration). Native C++ performance. Still isolated from core via `modules/` directory. |
| `AudioStreamPlayback` as the host interface | Plugs into Godot's existing playback lifecycle. Gets pre-allocation via `start()`, block execution via `mix()`, cleanup via destructor. Zero new engine concepts needed. |

### 2.3 Godot Audio Pipeline — Verified Properties (From Audit)

These properties of Godot's audio system have been verified through source code audit and are the foundation our design relies on:

| Property | Status | Implication for Symphony |
|----------|--------|--------------------------|
| Zero heap allocations in `_mix_step()` | ✅ Confirmed | Our `mix()` override must also be allocation-free. All buffers pre-allocated in `start()`. |
| `SafeNumeric<float>` with acquire/release semantics | ✅ Confirmed | We use this pattern for game thread → audio thread parameter passing. |
| Fixed 512-sample buffer passed to `mix()` | ✅ Confirmed | We subdivide internally into 64-sample micro-blocks (8 iterations per callback). |
| Single-threaded execution within `_mix_step()` | ✅ Confirmed | Zero-copy pointer passing between operators is safe. No reference counting needed. |
| `start()` called on main thread before audio thread sees the playback | ✅ Confirmed | This is our factory phase for arena allocation and graph compilation. |
| Deferred deletion via graveyard pattern | ✅ Confirmed | We follow the same pattern for graph hot-swap (old graph freed on main thread). |

---

## 3. Core Technical Design

### 3.1 The Operator Interface

Every DSP node in Symphony implements the `IOperator` interface:

```cpp
class IOperator {
public:
    virtual ~IOperator() = default;

    // Called once during graph compilation (main thread, non-RT safe)
    // Allocates internal state (delay lines, phase accumulators, history buffers)
    static IOperator* CreateOperator(const OperatorSettings& settings, const PinBindings& inputs);

    // Called every micro-block on the audio thread (RT safe, zero allocations)
    virtual void Execute(int32_t num_frames) = 0;

    // For hot-swap: export/import internal state
    virtual void ExportState(uint8_t* buffer, size_t* size) const {}
    virtual void ImportState(const uint8_t* buffer, size_t size) {}
};
```

### 3.2 Typed Pin System

Connections between operators are strongly typed. Each pin carries one of these data types:

| Pin Type | C++ Type | Size per Micro-Block | Description |
|----------|----------|---------------------|-------------|
| Audio | `float*` (64 floats) | 256 bytes | Contiguous audio sample buffer |
| Float | `float*` (single value) | 4 bytes | Control-rate scalar (updated once per micro-block) |
| Int | `int32_t*` (single value) | 4 bytes | Integer parameter |
| Bool | `bool*` (single value) | 1 byte | Gate/toggle |
| Trigger | `TriggerBuffer*` | Variable | Array of sample-offset trigger events |

Pins are connected via raw pointer assignment during graph compilation. An upstream operator's output buffer pointer is stored in the downstream operator's input slot. During `Execute()`, operators read directly from upstream memory — zero copy, zero indirection beyond one pointer dereference.

### 3.3 Trigger System (FTrigger Equivalent)

```cpp
struct TriggerEvent {
    int32_t sample_offset;  // Exact sample index within the micro-block (0-63)
    float value;            // Optional payload (e.g., velocity, note number)
};

struct TriggerBuffer {
    TriggerEvent events[MAX_TRIGGERS_PER_BLOCK];  // Fixed-size, no allocation
    int32_t count;
};
```

Operators that respond to triggers use the block-splitting execution pattern:

```cpp
void MyOperator::Execute(int32_t num_frames) {
    int32_t current_frame = 0;
    for (int32_t i = 0; i < trigger_in->count; i++) {
        int32_t trigger_frame = trigger_in->events[i].sample_offset;
        if (trigger_frame > current_frame) {
            ProcessAudio(current_frame, trigger_frame);  // Render up to trigger
            current_frame = trigger_frame;
        }
        ApplyTriggerAction(trigger_in->events[i]);  // State change on exact sample
    }
    if (current_frame < num_frames) {
        ProcessAudio(current_frame, num_frames);  // Render remainder
    }
}
```

### 3.4 Graph Compilation Pipeline

```
[Editor Graph (Resource)]
        │
        ▼
[Validation] → Cycle detection, type checking, missing connections
        │
        ▼
[Flattening] → Inline sub-graphs, resolve variables
        │
        ▼
[Topological Sort] → Kahn's algorithm → linear operator array
        │
        ▼
[Buffer Allocation] → Liveness analysis → arena size calculation
        │
        ▼
[Compiled Graph] → Flat array of IOperator*, pre-allocated arena, pin pointer table
```

Compilation happens on the main thread (or a worker thread). The compiled graph is atomically published to the audio thread via pointer swap.

### 3.5 Memory Model

```
┌─────────────────────────────────────────────────────┐
│  Arena (allocated in start(), freed in destructor)   │
├─────────────────────────────────────────────────────┤
│  [Buffer Slot 0: 64 floats] [Buffer Slot 1: 64 floats] ... [Buffer Slot N] │
│  [Operator State 0] [Operator State 1] ... [Operator State M]              │
└─────────────────────────────────────────────────────┘
```

- **Arena Allocator:** Single contiguous allocation during `AudioStreamPlaybackSymphony::start()`.
- **Buffer Slots:** Pre-allocated float arrays shared between operators via liveness analysis (v2) or one-per-output (v1).
- **Operator States:** Phase accumulators, filter coefficients, delay line memory — all within the arena.
- **Audio Thread:** Only reads/writes within the arena. Zero `malloc`/`new` calls.

### 3.6 Parameter Routing (Game Thread → Audio Thread)

```cpp
// Game thread (GDScript):
audio_player.get_stream_playback().set_parameter("engine_rpm", 4500.0)

// Internal: writes to SafeNumeric<float> or SPSC queue
parameter_slots[param_index].store(4500.0f, std::memory_order_release);

// Audio thread (top of micro-block loop):
float rpm = parameter_slots[param_index].load(std::memory_order_acquire);
// Feeds into graph input node
```

For v1, we use `SafeNumeric<float>` (one atomic per exposed parameter). For v2, if we need bulk parameter updates or non-float types, we add an SPSC ring buffer.

---

## 4. V1 Feature Scope

### 4.1 Built-in Node Library (~20 Nodes)

**Generators (5)**
| Node | Description |
|------|-------------|
| Oscillator | Sine, saw, square, triangle with audio-rate frequency input |
| Noise | White, pink noise generator |
| WavePlayer | Plays an AudioStreamWAV resource with pitch control |
| Constant | Outputs a fixed float value |
| LFO | Low-frequency oscillator for modulation (sub-audio rate) |

**Filters (4)**
| Node | Description |
|------|-------------|
| Biquad Filter | Low-pass, high-pass, band-pass, notch (audio-rate cutoff) |
| One-Pole Filter | Simple smoothing filter for parameter interpolation |
| DC Blocker | Removes DC offset from signal |
| Saturator | Soft/hard clipping distortion |

**Envelopes & Dynamics (3)**
| Node | Description |
|------|-------------|
| ADSR Envelope | Attack/Decay/Sustain/Release with trigger input |
| Gain | Amplitude scaling with audio-rate modulation |
| Compressor | Dynamic range compression |

**Math & Utility (4)**
| Node | Description |
|------|-------------|
| Math (Add/Multiply/Subtract) | Arithmetic on audio or float signals |
| Mix/Crossfade | Blend two signals with a mix parameter |
| Map Range | Remap a value from one range to another |
| Sample & Hold | Capture a value on trigger, hold until next trigger |

**Timing & Sequencing (2)**
| Node | Description |
|------|-------------|
| Clock | Generates periodic triggers at a given BPM |
| Trigger Delay | Delays a trigger by N samples or milliseconds |

**I/O (2)**
| Node | Description |
|------|-------------|
| Graph Input | Exposes a parameter to the game (GDScript API) |
| Graph Output | Final stereo output of the graph |

### 4.2 Visual Editor

- Built as `SymphonyEditorPlugin` in `modules/symphony/editor/`
- Uses Godot's `GraphEdit` and `GraphNode` widgets
- Features:
  - Drag-and-drop node creation from a categorized palette
  - Typed wire connections with color coding (audio = thick blue, float = thin green, trigger = dashed orange)
  - Connection validation (type mismatch prevention, cycle detection)
  - Undo/redo support via Godot's `EditorUndoRedoManager`
  - Copy/paste/duplicate nodes and sub-selections
  - Live preview via a hidden `AudioStreamPlayer` in the editor
  - Per-node parameter editing in the Inspector panel

### 4.3 GDScript API

```gdscript
# Playing a Symphony graph
var player = $AudioStreamPlayer
player.stream = preload("res://audio/engine_sound.symphony")
player.play()

# Runtime parameter control
var playback = player.get_stream_playback()
playback.set_parameter("engine_rpm", 4500.0)
playback.set_parameter("throttle", 0.8)

# Triggering events
playback.trigger("explosion")
playback.trigger("note_on", 0.9)  # with velocity payload

# Querying state
var pos = playback.get_playback_position()
var is_playing = playback.is_playing()
```

### 4.4 Sub-Graph Support

- A Symphony graph can contain "Patch" nodes that reference another Symphony graph resource.
- At compilation time, patches are inlined (flattened) into the parent graph.
- Patches expose their Graph Input/Output nodes as pins on the parent node.
- Enables DRY reuse: build a "Gunshot" patch once, use it in 50 different weapon graphs.

### 4.5 Basic Voice Management

- Each `AudioStreamPlaybackSymphony` instance is one "voice."
- A simple voice limiter: configurable max concurrent voices per graph type.
- Stealing policy: kill the oldest or quietest voice when the limit is exceeded.
- CPU budget tracking: measure execution time per voice, warn when approaching the audio deadline.
- No dynamic voice virtualization in v1 (that's a v2 feature).

---

## 5. Development Roadmap

### Phase 1: The Isolated Execution Sandbox (Weeks 1-2)

**Goal:** Prove DSP math works inside Godot's audio pipeline without crashing.

**Deliverables:**
- `modules/symphony/` directory with `SCsub`, `register_types.cpp`, `config.py`
- `AudioStreamSymphony` resource class (minimal, holds graph data)
- `AudioStreamPlaybackSymphony` with a hardcoded 3-node graph: Oscillator → Gain → Output
- The `mix()` override outputs a clean sine wave through Godot's standard pipeline
- Zero dynamic allocations during `mix()` — verified via custom `operator new` override in debug builds

**Constraints:**
- No visual UI
- No serialization
- No topological sorting (execution order is hardcoded)
- Nodes are instantiated manually in `start()`

**Validation:**
- Attach `AudioStreamSymphony` to an `AudioStreamPlayer` in a test scene
- Hear a sine wave at the correct frequency
- No audio glitches over 60 seconds of continuous playback
- Memory profiler shows zero allocations after `start()` completes

### Phase 2: Internal Subdivision & Trigger Prototyping (Week 3)

**Goal:** Achieve sample-accurate sub-block timing within the 512-sample buffer.

**Deliverables:**
- Internal micro-block loop: `mix()` iterates 8× over 64-sample chunks
- `TriggerBuffer` data type implemented
- `TriggerEvent` carries exact sample offset within micro-block
- ADSR envelope node that responds to triggers at precise sample positions
- Test: trigger an envelope at sample offset 35 — verify the attack starts exactly there (not at block boundary)

**Validation:**
- Record output to WAV file
- Visually inspect waveform in Audacity — envelope onset aligns with expected sample position
- Compare against a reference: trigger at offset 0 vs offset 35 must produce a 35-sample phase difference

### Phase 3: Topological Sorter & Pre-Allocation (Weeks 4-6)

**Goal:** Convert arbitrary node graphs into linear execution queues with pre-allocated memory.

**Deliverables:**
- `GraphCompiler` class:
  - Accepts a graph description (nodes + connections)
  - Validates: cycle detection, type checking, missing required connections
  - Topological sort (Kahn's algorithm) → produces ordered `IOperator*` array
  - Computes total arena size (sum of all operator state + output buffers)
- `ArenaAllocator`:
  - Single `malloc` during compilation
  - Bump-pointer allocation for operator states and buffers
  - `alignas(32)` for SIMD compatibility
- Pin binding:
  - After sort, resolve all input pins to point at upstream output buffer addresses
- Compiled graph struct:
  - `IOperator** operators` (sorted array)
  - `int32_t operator_count`
  - `uint8_t* arena` (single allocation)
  - `size_t arena_size`
- Atomic graph swap:
  - Main thread compiles new graph → stores pointer atomically
  - Audio thread picks up new graph at next block boundary
  - Old graph sent to graveyard for deferred deletion

**Also in this phase:**
- Design the GDScript API surface (even if not fully implemented yet)
- Design the custom node registration interface (for future third-party nodes)
- Error reporting: compilation errors stored as an array of diagnostic messages

**Validation:**
- Build a 10-node graph programmatically (no UI yet)
- Verify topological order respects all dependencies
- Verify zero allocations during `Execute()` loop
- Verify graph hot-swap: change a parameter mid-playback, hear the change without glitches

### Phase 4: Editor UI & Resource Serialization (Weeks 7-9)

**Goal:** Visual authoring workflow and persistence.

**Deliverables:**
- `SymphonyEditorPlugin` (`editor/plugins/symphony_editor_plugin.cpp`):
  - `GraphEdit`-based node canvas
  - Node palette (categorized: Generators, Filters, Envelopes, Math, Timing, I/O)
  - Wire drawing with type-based color coding
  - Connection validation (prevent type mismatches, detect cycles on connect)
  - Undo/redo via `EditorUndoRedoManager`
  - Copy/paste/duplicate
  - Inspector integration for per-node parameter editing
- Resource serialization:
  - `AudioStreamSymphony` saves/loads via standard Godot `Resource` system (`.tres` / `.res`)
  - Graph topology stored as node array + connection array
  - Sub-graph references stored as resource paths
- Live editor preview:
  - Hidden `AudioStreamPlayer` in editor viewport
  - Plays the current graph on demand (button or auto-play on change)
  - Recompiles graph on topology change, hot-swaps into playing instance
- GDScript API implementation:
  - `set_parameter(name, value)`
  - `trigger(name, payload)`
  - `is_playing()`
  - `get_playback_position()`

**Validation:**
- Create a graph visually in the editor
- Save, close, reopen — graph is identical
- Press "Preview" — hear the graph in the editor
- Connect the graph to an `AudioStreamPlayer` in a scene — plays correctly at runtime
- Call `set_parameter()` from GDScript — hear the change in real-time

### Phase 5: Profiling & Justified Optimization (Week 10+)

**Goal:** Stress-test under production load. Optimize only where measurements demand it.

**Deliverables:**
- Stress test: 100-500 concurrent Symphony voices with varying graph complexity
- Per-voice CPU cost measurement (rdtsc or high_resolution_clock)
- Voice management system:
  - Configurable max voices per graph type
  - Stealing policy (oldest / quietest / highest-cost)
  - CPU budget warning when approaching audio deadline
- Performance profiling:
  - Identify cache thrashing patterns → implement buffer liveness analysis if needed
  - Identify mutex contention → implement lock-free command queue if needed
  - Identify SIMD opportunities → add `__restrict__` and alignment hints to critical loops
- Cross-platform validation: Windows, Linux, macOS, Android, Web (if applicable)

**Optimization decisions gated behind data:**

| Observation | Response |
|-------------|----------|
| Buffer memory exceeds budget at high voice counts | Implement liveness analysis (register allocation for buffers) |
| Driver mutex causes measurable dropouts | Propose lock-free command queue patch to `audio_server.cpp` |
| Inner loops not auto-vectorizing | Add SIMD intrinsics or restructure loops |
| Graph recompilation causes audio glitch | Implement state migration (ExportState/ImportState) |
| Editor preview stutters on large graphs | Move compilation to worker thread |

---

## 6. Research Topics

These topics require study during Phases 3-5. None block Phase 1-2.

### 6.1 Incremental Topological Sorting

**Problem:** When a user connects/disconnects a wire during live editing, re-sorting the entire graph is O(V+E). For large graphs (100+ nodes) with frequent edits, this may cause perceptible latency.

**Key References:**
- Marchetti-Spaccamela, Nanni, Rohnert: *"Maintaining a Topological Order Under Edge Insertions"* (Information and Computation, 1996) — O(||δ|| + m) amortized per insertion
- Pearce & Kelly: *"A Dynamic Topological Sort Algorithm for DAGs"* (JEA, 2007) — practical implementation
- Bender, Fineman, Gilbert: *"A New Approach to Incremental Cycle Detection"* (ACM TALG, 2015) — for detecting illegal feedback loops during live editing

**When needed:** Phase 4 (editor), but only if full re-sort proves too slow for interactive editing. Start with naive Kahn's algorithm; upgrade if profiling shows latency.

### 6.2 Buffer Liveness Analysis

**Problem:** A 100-node graph doesn't need 100 output buffers simultaneously. If Node A's output is consumed only by Node B (which executes immediately after), Node A's buffer can be reused for Node C later.

**Key References:**
- Poletto & Sarkar: *"Linear Scan Register Allocation"* (ACM TOPLAS, 1999) — map "registers" to "buffer slots," "variables" to "node outputs"
- Faust compiler source code — performs exactly this optimization on generated C++ DSP code
- Conceptually identical to graph coloring register allocation, but linear scan is simpler and sufficient

**When needed:** Phase 5, only if memory pressure is observed at high voice counts. V1 uses one buffer per output pin (simple, correct, slightly wasteful).

### 6.3 Voice Stealing & CPU Budget Management

**Problem:** With N concurrent graph instances, the audio thread can exceed its deadline. Need a policy for which voices to kill or degrade.

**Key References:**
- Wwise voice management documentation (publicly available) — priority-based stealing, virtual voices, below-threshold behavior
- MetaSound's `Relative Render Cost` system — per-graph CPU measurement
- Practical: use `rdtsc` instruction (x86) or `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` (POSIX) to measure per-voice execution time with minimal overhead

**When needed:** Phase 5. Basic max-voice limiting is Phase 4. Sophisticated CPU-aware stealing is Phase 5.

### 6.4 Hot-Swap Graph Compilation (State Migration)

**Problem:** When the editor recompiles a graph while it's playing, the new graph must inherit state from the old graph (oscillator phases, filter histories, delay line contents) to avoid audible discontinuities.

**Key References:**
- JUCE `AudioProcessorGraph` — handles dynamic node addition/removal during playback
- Knyst (Rust) — lock-free graph mutation with "free graph" acknowledgment protocol
- Custom design needed: a "state snapshot" protocol where each operator exports/imports its internal state as a flat byte array

**When needed:** Phase 4 (editor live preview). Initial implementation can accept a brief audio restart on recompile; seamless hot-swap is a polish item.

### 6.5 SIMD Auto-Vectorization Patterns

**Problem:** Inner loops (oscillator phase accumulation, biquad filter, buffer mixing) must auto-vectorize for acceptable CPU performance at scale.

**Key References:**
- GCC/Clang auto-vectorization reports (`-fopt-info-vec-missed` / `-Rpass=loop-vectorize`)
- Key patterns: `__restrict__` pointers, `alignas(32)` buffers, loop trip counts known at compile time, no cross-iteration dependencies
- ARM NEON and x86 SSE2/AVX intrinsics for critical paths if auto-vectorization fails
- Study Godot's `servers/audio/effects/audio_filter_sw.cpp` for existing patterns

**When needed:** Phase 5. Write scalar code first, verify correctness, then optimize hot loops based on profiler data.

---

## 7. Technical Constraints & Risks

### 7.1 Hard Constraints (Non-Negotiable)

| Constraint | Reason |
|------------|--------|
| Zero heap allocations in `Execute()` / `mix()` | Real-time audio thread. `malloc` can trigger kernel locks → audio dropout. |
| Zero mutex acquisition in `Execute()` / `mix()` | Priority inversion → audio dropout. |
| Deterministic output | Same graph + same inputs + same parameter sequence = identical audio output. Required for testing and replays. |
| No modification to `servers/audio/` in v1 | Maintainability. Upstream compatibility. |
| All operator state pre-allocated before audio thread access | Dangling pointer prevention. Thread safety. |

### 7.2 Soft Constraints (Preferred but Negotiable)

| Constraint | Reason | Fallback |
|------------|--------|----------|
| 64-sample internal micro-block size | Good balance of granularity vs overhead | Configurable: 64, 128, or 256 |
| Sub-graph inlining at compile time | Simplifies runtime execution | Could support runtime sub-graph instantiation in v2 |
| Single-threaded DAG evaluation | Simplicity, no internal synchronization needed | Multi-threaded evaluation possible in v2 for very large graphs |

### 7.3 Known Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Driver mutex causes dropout during bus reconfiguration | Low | Medium | Document: "Don't reconfigure buses during gameplay." Defer lock-free queue to v2. |
| 8× micro-block iteration overhead too high for complex graphs | Medium | Medium | Make micro-block size configurable. Profile early in Phase 3. |
| Graph compilation too slow for interactive editor use | Low | Low | Move compilation to worker thread. Use incremental sort. |
| Upstream Godot changes `AudioStreamPlayback` interface | Low | High | Module isolation means only `mix()` signature matters. Monitor Godot PRs. |
| Voice count exceeds CPU budget without warning | High | High | Implement basic CPU measurement in Phase 5. Add hard voice limit. |

---

## 8. Module File Structure

```
modules/symphony/
├── SCsub                              # Build configuration
├── config.py                          # Module detection and dependencies
├── register_types.cpp                 # ClassDB registration
├── register_types.h
│
├── core/                              # Runtime execution engine
│   ├── symphony_operator.h            # IOperator interface
│   ├── symphony_operator.cpp
│   ├── symphony_graph_compiler.h      # Topological sort, validation, arena sizing
│   ├── symphony_graph_compiler.cpp
│   ├── symphony_arena_allocator.h     # Bump allocator for operator buffers
│   ├── symphony_arena_allocator.cpp
│   ├── symphony_trigger.h             # TriggerBuffer, TriggerEvent
│   ├── symphony_pin_types.h           # Pin type definitions and routing
│   └── symphony_compiled_graph.h      # Compiled graph struct (operator array + arena)
│
├── stream/                            # Godot integration layer
│   ├── audio_stream_symphony.h        # AudioStream resource (holds graph data)
│   ├── audio_stream_symphony.cpp
│   ├── audio_stream_playback_symphony.h   # AudioStreamPlayback (runtime executor)
│   └── audio_stream_playback_symphony.cpp
│
├── nodes/                             # Built-in operator implementations
│   ├── generators/
│   │   ├── symphony_oscillator.h/.cpp
│   │   ├── symphony_noise.h/.cpp
│   │   ├── symphony_wave_player.h/.cpp
│   │   ├── symphony_constant.h/.cpp
│   │   └── symphony_lfo.h/.cpp
│   ├── filters/
│   │   ├── symphony_biquad_filter.h/.cpp
│   │   ├── symphony_one_pole.h/.cpp
│   │   ├── symphony_dc_blocker.h/.cpp
│   │   └── symphony_saturator.h/.cpp
│   ├── envelopes/
│   │   ├── symphony_adsr.h/.cpp
│   │   ├── symphony_gain.h/.cpp
│   │   └── symphony_compressor.h/.cpp
│   ├── math/
│   │   ├── symphony_math_ops.h/.cpp
│   │   ├── symphony_mix.h/.cpp
│   │   ├── symphony_map_range.h/.cpp
│   │   └── symphony_sample_hold.h/.cpp
│   ├── timing/
│   │   ├── symphony_clock.h/.cpp
│   │   └── symphony_trigger_delay.h/.cpp
│   └── io/
│       ├── symphony_graph_input.h/.cpp
│       └── symphony_graph_output.h/.cpp
│
├── editor/                            # Editor plugin (only compiled in editor builds)
│   ├── symphony_editor_plugin.h/.cpp
│   ├── symphony_graph_edit.h/.cpp     # Custom GraphEdit subclass
│   ├── symphony_node_palette.h/.cpp   # Node creation menu
│   └── symphony_preview_player.h/.cpp # Editor audio preview
│
└── doc_classes/                       # XML class documentation
    ├── AudioStreamSymphony.xml
    └── AudioStreamPlaybackSymphony.xml
```

---

## 9. Reference Architectures

These open-source projects implement similar patterns and serve as implementation references:

| Project | Language | Key Takeaway for Symphony |
|---------|----------|---------------------------|
| [JUCE AudioProcessorGraph](https://juce.com) | C++ | Topological sort of audio processors, dynamic node add/remove, `AbstractFifo` for lock-free ring buffers |
| [Knyst](https://github.com/ErikNatanael/knyst) | Rust | Lock-free graph mutation, generational arena allocator, wait-free audio thread |
| [LabSound](https://github.com/LabSound/LabSound) | C++ | WebAudio-style graph with lock-free message passing between threads |
| [Pure Data / libpd](https://puredata.info/) | C | Visual patching paradigm, audio-rate vs control-rate signal separation |
| [Faust](https://faust.grame.fr/) | Functional | Static compilation of DSP graphs into optimized C++, buffer sharing via liveness analysis |
| Godot VisualShader | C++ | Graph compilation pipeline, `GraphEdit` editor UI, node registration pattern |

---

## 10. Success Criteria

### V1 is "Done" When:

1. A game developer can open the Symphony editor, visually build a procedural audio graph, and hear it play in their game — without writing C++.
2. Parameters can be modulated from GDScript at runtime with audio-rate smoothing.
3. Triggers fire with sample-accurate timing (verified by waveform inspection).
4. 50 concurrent Symphony voices play without audio dropout on a mid-range desktop CPU.
5. Sub-graphs (patches) can be created, saved, and reused across multiple parent graphs.
6. The module compiles cleanly against Godot `master` branch with zero modifications to any file outside `modules/symphony/`.

### V1 is NOT:

- A complete replacement for Godot's existing audio system (buses, effects, AudioStreamPlayer still work as before)
- A 1:1 MetaSound clone (we don't need Unreal's specific node set or Blueprint integration)
- Optimized for 500+ concurrent voices (that's v2)
- A lock-free replacement for Godot's driver mutex (that's v2, if data justifies it)

---

## 11. Open Questions (To Resolve During Development)

| Question | Phase | Notes |
|----------|-------|-------|
| Should the internal micro-block size be user-configurable or fixed at 64? | Phase 2 | Start fixed, make configurable if profiling shows need |
| How do we handle stereo vs mono signals in the pin system? | Phase 1 | Options: always stereo (simple), typed mono/stereo pins (flexible), auto-upmix |
| What's the file extension for Symphony graphs? | Phase 4 | `.symphony`, `.sgraph`, `.tres` with custom type? |
| Should sub-graphs be inlined at compile time or instantiated at runtime? | Phase 3 | Inlining is simpler and faster; runtime instantiation allows dynamic patching |
| How do we expose Symphony to GDExtension developers for custom node authoring? | Phase 5+ | Need a stable C API for `IOperator` registration |
| Do we support multi-channel (surround) output or stereo only in v1? | Phase 1 | Stereo only for v1. Multi-channel is a v2 feature. |

---

## 12. Glossary

| Term | Definition |
|------|------------|
| **DAG** | Directed Acyclic Graph — the mathematical structure of the node graph (no feedback loops without explicit delay nodes) |
| **Operator** | A single DSP processing unit (one node in the graph). Implements `IOperator::Execute()`. |
| **Micro-block** | The internal processing granularity (64 samples). The 512-sample driver buffer is subdivided into 8 micro-blocks. |
| **Arena** | A single contiguous memory allocation that holds all operator states and audio buffers for one graph instance. |
| **Pin** | A typed input or output connection point on an operator. Carries audio, float, int, bool, or trigger data. |
| **Trigger** | A discrete event with a precise sample offset. Used for note-on, envelope start, sequencer beats. |
| **Compiled Graph** | The output of the graph compiler: a flat array of operators in topological order + pre-allocated arena + pin pointer table. |
| **Hot-Swap** | Replacing a running graph's compiled representation with a new version without stopping playback. |
| **Voice** | One active instance of a Symphony graph (one `AudioStreamPlaybackSymphony`). |
| **Patch** | A reusable sub-graph that can be embedded in other graphs. Flattened at compile time. |
| **Liveness Analysis** | Compiler optimization that determines which buffers can be reused because their data is no longer needed by any downstream operator. |

---

*Document created: May 21, 2026*
*Module name: Symphony*
*Target engine: Godot (master branch)*
*Architecture: C++ Module (`modules/symphony/`)*
*V1 scope: ~20 nodes, visual editor, GDScript API, sub-graphs, basic voice management*
