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
[AudioStreamSymphony Resource (.tres)]
        │
        ▼
[GraphFlattener::flatten()] → Inline SubGraph nodes, remap IDs, cycle detection
        │
        ▼
[GraphCompiler::compile()]
  ├─ [Validation] → Type checking, missing required connections, unknown types
  ├─ [Topological Sort] → Kahn's algorithm → linear operator array
  ├─ [Arena Sizing] → Sum of operator states + output buffers + promotions
  ├─ [Operator Creation] → Placement new in arena via create_fn
  └─ [Pin Binding] → Input pointers → upstream output buffer addresses
        │
        ▼
[CompiledGraph] → Flat array of SymphonyOperator*, pre-allocated arena, promotion table
```

Compilation happens on the main thread. The compiled graph is atomically published to the audio thread via `swap_graph()` pointer swap. Old graph freed on main thread via deferred deletion.

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

### 4.1 Built-in Node Library (21 Nodes — All Implemented)

**Generators (4)**
| Node | Description |
|------|-------------|
| Oscillator | Sine, saw, square, triangle with audio-rate frequency input |
| Noise | White, pink noise generator |
| Constant | Outputs a fixed float value |
| LFO | Low-frequency oscillator for modulation (Float output, connects via implicit promotion) |

**Filters (4)**
| Node | Description |
|------|-------------|
| Biquad Filter | Low-pass, high-pass, band-pass, notch (audio-rate cutoff modulation, per-micro-block coefficient recalc) |
| One-Pole Filter | Simple smoothing filter for parameter interpolation |
| DC Blocker | Removes DC offset from signal |
| Saturator | Soft/hard clipping distortion |

**Envelopes & Dynamics (3)**
| Node | Description |
|------|-------------|
| ADSR Envelope | Attack/Decay/Sustain/Release with trigger input (sample-accurate block-splitting) |
| Gain | Amplitude scaling with audio-rate modulation |
| Compressor | Peak-based dynamic range compression (RMS mode reserved for future) |

**Math & Utility (4)**
| Node | Description |
|------|-------------|
| MathAdd | Audio-rate addition (A + B) |
| Mix | Crossfade blend two signals with a mix parameter |
| Map Range | Remap a value from one range to another |
| Sample & Hold | Capture a value on trigger, hold until next trigger |

**Timing & Sequencing (2)**
| Node | Description |
|------|-------------|
| Clock | Generates periodic triggers at a given BPM |
| Trigger Delay | Delays a trigger by N samples or milliseconds |

**I/O (4)**
| Node | Description |
|------|-------------|
| GraphInput | Exposes a parameter to the game (GDScript API). Configurable pin_type, sort_order, display_name. |
| GraphOutput | Final stereo output of the graph. Configurable pin_type, sort_order, display_name. |
| TriggerInput | Exposes a trigger event to the game (GDScript API). |
| SubGraph | References another AudioStreamSymphony resource. Flattened at compile time. Dynamic pins from referenced graph's I/O nodes. |

**Deferred:**
| Node | Status |
|------|--------|
| WavePlayer | Deferred — needs resource picker widget (not just SpinBox) |

### 4.2 Visual Editor ✅ IMPLEMENTED

- Built as `SymphonyEditorPlugin` in `modules/symphony/editor/`
- Uses Godot's `GraphEdit` and `GraphNode` widgets (bottom panel)
- Features:
  - Categorized Add Node menu (sub-menus by category, sanitized names for Godot node system)
  - Typed wire connections with color coding (Audio=blue, Float=green, Int=yellow, Bool=white, Trigger=orange)
  - Connection validation (type mismatch prevention via `add_valid_connection_type`, Float→Audio promotion allowed)
  - Undo/redo support via `EditorUndoRedoManager` (all operations: add/remove node, connect/disconnect, move, param change, frames)
  - Copy/paste/duplicate nodes with cross-graph static clipboard and ID remapping
  - Comment frames using `GraphFrame` (resizable, title editable via Inspector)
  - Live preview via hidden `AudioStreamPlayer` (hot-swap recompile on topology change)
  - Per-node parameter editing via inline SpinBoxes + Inspector panel (bidirectional sync)
  - Collapsible parameters (toggle button on titlebar, node resizes)
  - Delete button on toolbar + Delete key support
  - Save button (first save → file dialog, subsequent → overwrite)
  - SubGraph breadcrumb navigation (double-click to enter, breadcrumb bar to go back)
  - Pin type dropdown (OptionButton) on GraphInput/GraphOutput nodes
  - Sort order SpinBox on I/O nodes

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

### 4.4 Sub-Graph Support ✅ IMPLEMENTED

- Every Symphony graph is a potential sub-graph. Any graph with `GraphInput`/`GraphOutput` nodes can be referenced by another graph.
- `SubGraph` nodes reference another `AudioStreamSymphony` resource via `resource_path` param.
- At compilation time, `GraphFlattener` recursively inlines sub-graphs into the parent (pre-pass before `GraphCompiler`).
- Sub-graph pins are derived from the referenced graph's `GraphInput`/`GraphOutput` nodes, sorted by `(sort_order, editor_y, name)`.
- Cycle detection: visited-path set + depth limit (32 levels). Compile error on circular reference.
- Editor: "Add SubGraph (from file)..." opens FileDialog. "Create New SubGraph..." creates empty graph and prompts save.
- Editor: Double-click SubGraph node → breadcrumb navigation into the sub-graph. Preview works at any depth.
- Supports both external (separate `.tres`) and embedded (inline sub-resource) references via Godot's native resource system.

### 4.5 Basic Voice Management

- Each `AudioStreamPlaybackSymphony` instance is one "voice."
- A simple voice limiter: configurable max concurrent voices per graph type.
- Stealing policy: kill the oldest or quietest voice when the limit is exceeded.
- CPU budget tracking: measure execution time per voice, warn when approaching the audio deadline.
- No dynamic voice virtualization in v1 (that's a v2 feature).

---

## 5. Development Roadmap

### Phase 1: The Isolated Execution Sandbox ✅ COMPLETE

**Delivered:**
- `modules/symphony/` directory with `SCsub`, `register_types.cpp`, `config.py`
- `AudioStreamSymphony` resource class
- `AudioStreamPlaybackSymphony` with micro-block execution
- Clean sine wave output through Godot's standard pipeline
- Zero dynamic allocations during `mix()`

### Phase 2: Internal Subdivision & Trigger Prototyping ✅ COMPLETE

**Delivered:**
- Internal 64-sample micro-block loop (8 iterations per 512-sample callback)
- `TriggerBuffer` / `TriggerEvent` with sample-accurate offsets
- ADSR envelope responding to triggers at precise sample positions
- `SymphonyTriggerInput` node for game-thread trigger routing

### Phase 3: Topological Sorter & Pre-Allocation ✅ COMPLETE

**Delivered:**
- `GraphCompiler` with full validation, cycle detection, type checking
- Kahn's algorithm topological sort
- `ArenaAllocator` with single contiguous allocation
- Pin binding (input pointers → upstream output buffers)
- `CompiledGraph` struct with operator array + arena
- Atomic graph hot-swap via `swap_graph()` on audio thread
- Float→Audio implicit promotion (compiler auto-fills 64-sample buffer from single float)
- `SafeNumeric<float>` parameter routing (game thread → audio thread)

### Phase 4: Editor UI & Resource Serialization ✅ COMPLETE

**Delivered (4A — Editor Polish):**
- `SymphonyEditorPlugin` with `GraphEdit`-based canvas
- Categorized Add Node menu (sub-menus by category)
- Typed wire connections with color coding
- Connection validation (type mismatch prevention via GraphEdit valid_connection_types)
- Undo/redo via `EditorUndoRedoManager` for all operations
- Copy/paste/duplicate with cross-graph static clipboard and ID remapping
- Comment frames using `GraphFrame` (resizable, title editable)
- Inspector integration (`SymphonyNodeInspectorProxy` with dynamic property list)
- Collapsible parameters (toggle button on titlebar)
- Live preview via hidden `AudioStreamPlayer`
- Save button (first save → `save_resource_as()`, subsequent → `ResourceSaver::save()`)
- Delete button on toolbar + Delete key support
- Per-node parameter editing via inline SpinBoxes (bidirectional sync with Inspector)

**Delivered (4B — Node Library):**
- 20 operators total across 6 categories:
  - Generators (4): Oscillator, Constant, Noise, LFO
  - Filters (4): BiquadFilter, OnePole, DCBlocker, Saturator
  - Envelopes (3): Gain, ADSR, Compressor
  - Math (4): MathAdd, Mix, MapRange, SampleHold
  - Timing (2): Clock, TriggerDelay
  - I/O (4): GraphInput, GraphOutput, TriggerInput, SubGraph

**Delivered (4C — Sub-Graph / Patch Support):**
- `SubGraph` node type referencing another `AudioStreamSymphony` resource
- `GraphFlattener` pre-pass: recursive inlining with ID remapping
- Cycle detection via visited-path set (depth limit: 32)
- `GraphInput`/`GraphOutput` upgraded with configurable `pin_type`, `sort_order`, `display_name`
- Pin ordering: `sort_order` ASC → `editor_position.y` ASC → `name` ASC
- Editor: dynamic pin rendering on SubGraph nodes (reads referenced resource's I/O)
- Editor: "Add SubGraph (from file)..." with FileDialog
- Editor: "Create New SubGraph..." (creates empty graph with one input + one output)
- Editor: `pin_type` OptionButton dropdown on I/O nodes
- Editor: Breadcrumb navigation (double-click SubGraph → navigate in, breadcrumb bar to go back)
- Preview works at any navigation depth (plays currently-viewed graph)

**Delivered (Resource Serialization):**
- Full `.tres` serialization via `_get_property_list` / `_get` / `_set`
- Nodes, connections, frames, params all persisted
- Sub-graph references stored as resource paths

**Delivered (GDScript API):**
- `set_parameter(name, value)` — routes to `GraphInput` nodes via `SafeNumeric<float>`
- `trigger(name)` — routes to `TriggerInput` nodes
- Playback lifecycle via standard `AudioStreamPlayer`

**Not yet delivered (deferred to Phase 5):**
- WavePlayer node (needs resource picker widget)
- Extract-to-SubGraph refactoring operation
- AUDIO-type `GraphInput` (pass-through audio from parent into sub-graph)
- State migration for seamless hot-swap (currently brief restart on recompile)

### Phase 5: Profiling & Voice Management (NEXT)

**Goal:** Measure real performance, implement voice management, validate tiers 1-3.

**Planned Deliverables:**
- Built-in profiling: per-voice timing, total budget %, peak tracking, memory per voice
- GDScript API: `get_voice_cpu_microseconds()`, `get_budget_percent()`
- Voice manager: priority-based stealing (priority + quietest), global limit, budget thresholds
- `voice_priority` property on AudioStreamSymphony (designer-facing) + runtime override
- RMS tracking per voice (for quietest-first stealing)
- GDScript stress-test scene (configurable voices, complexity, HUD with metrics)
- Trivial optimizations: `__restrict__` on pin pointers, loop hints
- Measurement runs for tiers 1-3 (10, 25, 50 voices)

**Target:** 50 mixed-complexity voices < 40% budget on M2 macOS (512-sample buffer @ 44.1kHz)

**Deferred to Phase 6:** Web testing, SIMD intrinsics, buffer liveness analysis, worker thread compilation, voice tiers 4+5 (80, 120)

### Phase 6: Optimize For Quest (Web + Scale)

**Goal:** Web export validation, advanced optimizations driven by Phase 5 data, scale to 80-120 voices.

**Planned Deliverables:**
- Web export (Emscripten/WASM) — verify module compiles and runs
- Web performance target: 25 voices < 60% budget (128-sample buffer, 2.9ms deadline)
- Advanced optimizations as needed: SIMD intrinsics, buffer liveness, worker thread compilation
- Voice tiers 4+5 (80, 120 voices) stress testing
- Cross-platform validation (Windows, Linux, Web)
- WavePlayer node implementation (resource picker widget)
- AUDIO-type GraphInput (audio pass-through into sub-graphs)

**Depends on:** Phase 5 profiling data to determine which optimizations are needed.

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

## 8. Module File Structure (Current)

```
modules/symphony/
├── SCsub                              # Build configuration (globs *.cpp in each subdir)
├── config.py                          # Module detection
├── register_types.cpp                 # ClassDB + OperatorRegistry registration
├── register_types.h
│
├── core/                              # Runtime execution engine
│   ├── symphony_operator.h            # SymphonyOperator base class (bind_pins + execute)
│   ├── symphony_operator_registry.h   # OperatorDescriptor + singleton registry
│   ├── symphony_operator_registry.cpp
│   ├── symphony_graph_description.h   # NodeDesc, ConnectionDesc, FrameDesc, GraphDescription
│   ├── symphony_graph_compiler.h      # GraphCompiler::compile() — validation, topo sort, arena
│   ├── symphony_graph_compiler.cpp
│   ├── symphony_graph_flattener.h     # GraphFlattener::flatten() — SubGraph inlining
│   ├── symphony_graph_flattener.cpp
│   ├── symphony_compiled_graph.h      # CompiledGraph struct (operator array + arena + execute)
│   ├── symphony_arena_allocator.h     # Bump allocator (single malloc, aligned alloc)
│   ├── symphony_pin_types.h           # SymphonyPinType enum + MICRO_BLOCK_SIZE constant
│   └── symphony_trigger.h             # TriggerBuffer, TriggerEvent (sample-accurate)
│
├── stream/                            # Godot integration layer
│   ├── audio_stream_symphony.h        # AudioStream resource (holds GraphDescription)
│   ├── audio_stream_symphony.cpp      # Serialization, compile_graph(), test graph builder
│   ├── audio_stream_playback_symphony.h   # AudioStreamPlayback (mix loop, hot-swap)
│   └── audio_stream_playback_symphony.cpp
│
├── nodes/                             # Built-in operator implementations (header-only)
│   ├── generators/
│   │   ├── symphony_oscillator.h      # Sine/saw/square/triangle, audio-rate freq input
│   │   ├── symphony_constant.h        # Fixed float value output
│   │   ├── symphony_noise.h           # White/pink noise
│   │   └── symphony_lfo.h            # Low-frequency oscillator (Float output)
│   ├── filters/
│   │   ├── symphony_biquad_filter.h   # LP/HP/BP/Notch with audio-rate cutoff
│   │   ├── symphony_one_pole.h        # Simple smoothing filter
│   │   ├── symphony_dc_blocker.h      # DC offset removal
│   │   └── symphony_saturator.h       # Soft/hard clipping
│   ├── envelopes/
│   │   ├── symphony_gain.h            # Amplitude scaling
│   │   ├── symphony_adsr.h            # Attack/Decay/Sustain/Release with trigger
│   │   └── symphony_compressor.h      # Peak-based dynamic range compression
│   ├── math/
│   │   ├── symphony_math_add.h        # Audio-rate addition (A + B)
│   │   ├── symphony_mix.h            # Crossfade blend
│   │   ├── symphony_map_range.h       # Value remapping
│   │   └── symphony_sample_hold.h     # Capture on trigger, hold until next
│   ├── timing/
│   │   ├── symphony_clock.h           # Periodic trigger generator (BPM)
│   │   └── symphony_trigger_delay.h   # Delay trigger by N samples
│   └── io/
│       ├── symphony_graph_input.h     # Game→audio parameter (SafeNumeric), configurable pin_type
│       ├── symphony_graph_output.h    # Final output to AudioFrame buffer, configurable pin_type
│       ├── symphony_trigger_input.h   # Game→audio trigger routing
│       └── symphony_subgraph.h        # SubGraph reference node (flattener-only, no create_fn)
│
└── editor/                            # Editor plugin (TOOLS_ENABLED only)
    ├── symphony_editor_plugin.h       # SymphonyEditorPlugin, SymphonyGraphEditor, InspectorProxy
    └── symphony_editor_plugin.cpp     # Full editor: GraphEdit, menus, undo/redo, breadcrumbs, preview
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

## 11. Open Questions (Status)

| Question | Phase | Resolution |
|----------|-------|------------|
| Should the internal micro-block size be user-configurable or fixed at 64? | Phase 2 | **Fixed at 64.** `SYMPHONY_MICRO_BLOCK_SIZE` is a compile-time constant. |
| How do we handle stereo vs mono signals in the pin system? | Phase 1 | **Mono internally.** GraphOutput copies mono to stereo AudioFrame (L=R). Multi-channel is v2. |
| What's the file extension for Symphony graphs? | Phase 4 | **`.tres`** — standard Godot resource format. Custom serialization via `_get_property_list`. |
| Should sub-graphs be inlined at compile time or instantiated at runtime? | Phase 3 | **Inlined at compile time** via `GraphFlattener` pre-pass. Zero runtime overhead. |
| How do we expose Symphony to GDExtension developers for custom node authoring? | Phase 5+ | **Open.** Need a stable C API for `OperatorDescriptor` registration. |
| Do we support multi-channel (surround) output or stereo only in v1? | Phase 1 | **Stereo only.** GraphOutput writes mono→stereo. |
| Float→Audio type conversion? | Phase 4B | **Implicit promotion.** Compiler auto-fills 64-sample buffer from single float value. |
| Sub-graph pin ordering? | Phase 4C | **sort_order ASC → editor_y ASC → name ASC.** Visible SpinBox on I/O nodes. |
| Sub-graph cycle detection? | Phase 4C | **Compile-time only.** Visited-path set + depth limit 32. Edit-time detection deferred. |
| Sub-graph preview behavior? | Phase 4C | **Preview plays current graph only** (MetaSound pattern). Navigate into sub-graph to preview it. |

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
*Last updated: May 30, 2026*
*Module name: Symphony*
*Target engine: Godot 4.6 (master branch)*
*Architecture: C++ Module (`modules/symphony/`)*
*Current status: Phases 1-4 complete. Phase 5 (profiling & optimization) next.*
*Node count: 21 operators implemented (4 generators, 4 filters, 3 envelopes, 4 math, 2 timing, 4 I/O)*
