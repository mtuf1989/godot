# Symphony GPU DSP Backend — Design Document v2.0

## Status: PLANNED (Future Implementation)

---

## 1. Motivation

Symphony v1.0 runs entirely on the CPU audio thread. While the micro-block architecture (32/64 samples) is efficient, compute-heavy operators like ModalBank, PhaseVocoder, and GrainCloud scale poorly when many voices are active simultaneously. A game with 30+ concurrent impact sounds, each running 64-mode ModalBanks, consumes significant CPU budget.

**Goal:** Offload embarrassingly-parallel DSP workloads to the GPU via Godot's RenderingDevice compute shaders, freeing CPU headroom for game logic, physics, and AI.

**Design Principle:** Squeeze the GPU as much as possible. The GPU is typically underutilized in audio-heavy games — we exploit that idle capacity.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│  Main Thread (Game Frame)                                     │
│                                                               │
│  ┌─────────────────────┐     ┌─────────────────────────────┐ │
│  │ GraphCompiler        │     │ GPUDSPBatch (singleton)      │ │
│  │ - Tags GPU-eligible  │────▶│ - Collects active GPU ops    │ │
│  │   operators at       │     │ - Updates params via SSBO    │ │
│  │   compile time       │     │ - Dispatches compute shaders │ │
│  └─────────────────────┘     │ - Manages triple buffer      │ │
│                               └──────────────┬──────────────┘ │
│                                              │                 │
│                                    ┌─────────▼─────────┐      │
│                                    │  RenderingDevice   │      │
│                                    │  (Local Instance)  │      │
│                                    │  submit() per frame│      │
│                                    └─────────┬─────────┘      │
│                                              │ GPU             │
└──────────────────────────────────────────────┼────────────────┘
                                               │
                                    ┌──────────▼──────────┐
                                    │  GPU Compute         │
                                    │  - modal_bank.glsl   │
                                    │  - fft_forward.glsl  │
                                    │  - fft_inverse.glsl  │
                                    │  - grain_sum.glsl    │
                                    │  - fdn_reverb.glsl   │
                                    └──────────┬──────────┘
                                               │
                                    ┌──────────▼──────────┐
                                    │  Triple Buffer       │
                                    │  (GPU-written PCM)   │
                                    │  [N-2] ← audio reads │
                                    │  [N-1] ← settling    │
                                    │  [N]   ← GPU writes  │
                                    └──────────┬──────────┘
                                               │
┌──────────────────────────────────────────────┼────────────────┐
│  Audio Thread                                │                 │
│                                              │                 │
│  ┌───────────────────────────────────────────▼───────────────┐│
│  │ CompiledGraph::execute()                                   ││
│  │  - CPU operators execute normally (topological order)      ││
│  │  - GPU proxy operators read from triple buffer [N-2]       ││
│  │  - Fallback: if GPU unavailable, execute CPU path          ││
│  └───────────────────────────────────────────────────────────┘│
└───────────────────────────────────────────────────────────────┘
```

### Key Decisions

1. **Local RenderingDevice** — Not the main RD. Audio compute is independent of rendering. A local RD on its own thread avoids contending with the render frame. We submit and sync on our own schedule.

2. **Triple-buffer** — GPU writes buffer N, buffer N-1 is in-flight settling, audio thread reads buffer N-2. This decouples GPU timing from audio callback timing.

3. **Batch dispatch** — All active ModalBanks (or PhaseVocoders, etc.) across all voices are batched into a single dispatch. This amortizes dispatch overhead and maximizes GPU occupancy.

4. **Transparent fallback** — Each GPU-eligible operator has a CPU fallback (the existing implementation). If the GPU backend is unavailable (Compatibility renderer, headless mode, mobile with insufficient limits), operators seamlessly run on CPU.

---

## 3. GPU-Eligible Operators

### 3.1 ModalBank — Priority: HIGHEST

**Why:** Each mode is an independent 2nd-order IIR (biquad resonator). With N modes across M voices, we have N×M independent state machines. Currently the inner loop is:

```cpp
for (int m = 0; m < num_modes; m++) {
    y = b0[m] * x - a1[m] * y1[m] - a2[m] * y2[m];
    y2[m] = y1[m]; y1[m] = y;
    sum += y * gain[m];
}
```

**GPU mapping:**
- One workgroup per voice, one thread per mode.
- Each thread computes its mode for the entire micro-block (32/64 samples) sequentially.
- After all samples computed, an intra-workgroup reduction sums all modes.
- Output: one float per sample per voice → written to the output SSBO.

**SSBO Layout:**
```glsl
// Set 0, Binding 0: Mode coefficients (read-only, updated on param change)
struct ModeCoeffs {
    float b0, a1, a2, gain;  // 16 bytes per mode
};

// Set 0, Binding 1: Mode state (read/write, persistent across frames)
struct ModeState {
    float y1, y2;  // 8 bytes per mode
};

// Set 0, Binding 2: Excitation input (written by CPU each frame)
// float excitation[MICRO_BLOCK_SIZE] per voice

// Set 0, Binding 3: Audio output (read by CPU after completion)
// float output[MICRO_BLOCK_SIZE] per voice
```

**Push Constants (16 bytes):**
```glsl
layout(push_constant) uniform Params {
    uint num_modes;
    uint num_voices;
    uint micro_block_size;
    uint _pad;
};
```

**Estimated gain:** 20+ voices × 64 modes = 1280 parallel threads. At 32 samples each = 40,960 operations. GPU breakeven at ~8 voices.

### 3.2 PhaseVocoder (FFT) — Priority: HIGH

**Why:** FFT is a textbook GPU workload. The per-bin magnitude/phase/unwrap/shift operations are perfectly parallel across num_bins (1025 for FFT 2048).

**GPU mapping:**
- Pass 1: Windowed frame → GPU FFT (Stockham radix-2 in compute)
- Pass 2: Per-bin analysis (mag, phase unwrap, pitch shift) — 1 thread per bin
- Pass 3: IFFT → windowed output
- Pass 4: Overlap-add accumulation

**Complexity:** Requires implementing a GPU FFT (no built-in in Godot). Stockham radix-2 auto-sort algorithm is well-suited for compute shaders (log2(N) passes, each pass reads from previous pass via barrier).

**Estimated gain:** Multiple simultaneous time-stretch/pitch-shift streams batched into one dispatch. Research shows 10× for large FFTs.

### 3.3 GrainCloud — Priority: MODERATE

**Why:** Per-grain windowed read + overlap-add is parallel across grains and voices. With 8 grains × many voices, parallelism scales with voice count.

**GPU mapping:**
- One thread per grain across all active GrainCloud voices.
- Each thread: read source PCM from buffer texture, apply window, write to per-voice accumulator.
- Final pass: reduce grain outputs to mono per voice.

**Limitation:** Source PCM must be uploaded to GPU memory. For live-input mode, this adds latency. Best for buffer-mode granulation.

### 3.4 FDN Reverb — Priority: MODERATE

**Why:** The matrix feedback operation and per-line filtering parallelize across the 8-16 delay lines. Reverb is typically a singleton effect (bus-level), so parallelism is across delay lines, not voices.

**GPU mapping:**
- Delay line memory stored as circular buffers in SSBO.
- One thread per delay line × per sample.
- Matrix multiplication as a workgroup-shared operation.

**Limitation:** Feedback topology creates frame-to-frame dependency. Must process one sample at a time within a micro-block (sequential within a thread, parallel across lines).

### 3.5 NOT GPU-Eligible

| Operator | Reason |
|----------|--------|
| SVFilter, BiquadFilter | Serial IIR dependency — each sample needs previous output |
| ADSR, EnvelopeFloat | Sequential state machine, trivial CPU cost |
| Oscillator, LFO | Too cheap — dispatch overhead exceeds compute savings |
| DelayLine | Memory-bound, serial read/write |
| ParameterSmoother | Single float state, negligible cost |
| WavePlayer | Memory-bound streaming, no parallelism |

---

## 4. Synchronization Strategy

### 4.1 Triple-Buffer Ring

```
Buffer Slot:  [0]         [1]         [2]
              │           │           │
Frame N:      Audio Read  Settling    GPU Write
Frame N+1:    (stale)     Audio Read  Settling    → GPU writes to [0]
Frame N+2:    GPU Write   (stale)     Audio Read
```

- **Audio thread** always reads from the oldest completed buffer (N-2). Lock-free — just reads an atomic index.
- **GPU thread** writes to the newest slot. Advances the write index atomically after `sync()` confirms completion.
- **Latency cost:** 2 micro-blocks = 64-128 samples = 1.3-2.7ms at 48kHz.

### 4.2 Parameter Upload

Parameters (excitation signal, coefficient changes) flow CPU → GPU:
- `buffer_update()` on the local RD before `compute_list_begin()`.
- Excitation audio is copied from the CPU-side input buffer each frame.
- Coefficient updates only happen on parameter change (not every frame).

### 4.3 Frame Timing

```
Game Frame Start
  ├── VoiceManager reports active GPU voices
  ├── GPUDSPBatch collects excitation buffers from all GPU proxy operators
  ├── buffer_update() — upload excitation + any param changes
  ├── compute_list_begin()
  │     ├── Dispatch: modal_bank.glsl (all voices batched)
  │     ├── barrier
  │     ├── Dispatch: fft_forward.glsl (all PV voices)
  │     ├── barrier
  │     ├── Dispatch: phase_vocoder_process.glsl
  │     ├── barrier
  │     ├── Dispatch: fft_inverse.glsl
  │     ├── barrier
  │     ├── Dispatch: grain_sum.glsl
  ├── compute_list_end()
  ├── submit()
  ├── sync() — BLOCKS until GPU done (acceptable on dedicated compute thread)
  ├── Advance triple-buffer write index (atomic)
Game Frame End

Audio Callback (independent timing):
  ├── Read triple-buffer at index (write_index - 2)
  ├── Copy GPU output into operator's audio_out pin
  ├── Continue normal graph execution
```

### 4.4 Dedicated Compute Thread

The `sync()` call blocks. We cannot do this on the main thread or audio thread. Solution:

- **SymphonyGPUThread** — a dedicated thread that:
  1. Waits for a signal from main thread (new frame data ready)
  2. Uploads buffers, dispatches, submits, syncs
  3. Advances the triple-buffer index
  4. Signals completion (optional, for profiling)

This thread runs at audio frame rate (~750 Hz for 64-sample blocks at 48kHz) or game frame rate (60Hz with batched multi-block dispatch), whichever is more efficient.

**Preferred: Game-frame-rate dispatch with multi-block batching.** Dispatch once per game frame, computing 4-16 micro-blocks worth of audio ahead. This reduces dispatch overhead and gives the GPU more work per dispatch. The triple buffer holds multiple micro-blocks.

---

## 5. GPU Proxy Operator Pattern

Each GPU-eligible operator gets a corresponding "proxy" that sits in the compiled graph:

```cpp
class SymphonyModalBankGPU : public SymphonyOperator {
    // Registered with GPUDSPBatch at compile time.
    // On execute(): reads from triple buffer instead of computing.
    int32_t gpu_voice_slot = -1;  // Index into batched GPU output
    float *gpu_output_buffer = nullptr;  // Points to triple-buffer read slot

    void execute(int32_t p_num_frames) override {
        if (gpu_output_buffer) {
            memcpy(audio_out, gpu_output_buffer + (frame_offset * p_num_frames),
                   sizeof(float) * p_num_frames);
        } else {
            // Fallback: run CPU implementation
            cpu_fallback.execute(p_num_frames);
        }
    }
};
```

The proxy is nearly zero-cost on the audio thread — just a `memcpy`.

---

## 6. Fallback Strategy

| Condition | Behavior |
|-----------|----------|
| Compatibility renderer (no RD) | All operators run CPU path |
| Headless / dedicated server | All operators run CPU path |
| Mobile with insufficient limits | All operators run CPU path |
| GPU backend initialized but voice count < threshold | Run on CPU (dispatch not worth it) |
| GPU backend stalls / sync timeout | Log warning, fall back to CPU for that frame |
| User preference `symphony/gpu_dsp_enabled = false` | All operators run CPU path |

The threshold (crossover point) will be determined empirically:
- **ModalBank:** likely ~4-8 voices before GPU wins
- **PhaseVocoder:** likely ~2 voices (FFT has high fixed overhead on CPU)
- **GrainCloud:** likely ~6-10 voices

---

## 7. Implementation Plan (Phased)

### Phase GPU-1: Foundation (2 weeks)

- [ ] `SymphonyGPUBatch` singleton — lifecycle management, local RD creation, device limit validation
- [ ] `SymphonyGPUThread` — dedicated thread with signal/wait loop
- [ ] Triple-buffer allocator — manages output ring per batch
- [ ] `GPUDSPCapability` query — checks renderer, device limits, returns enabled/disabled
- [ ] Project setting: `symphony/gpu_dsp_enabled` (bool, default true)
- [ ] Project setting: `symphony/gpu_batch_frames` (int, default 8 — micro-blocks per dispatch)

### Phase GPU-2: ModalBank on GPU (2 weeks)

- [ ] `modal_bank.glsl` compute shader — per-mode parallel biquad + workgroup reduction
- [ ] `SymphonyModalBankGPU` proxy operator
- [ ] Batch registration: compiler detects ModalBank nodes, registers with GPUDSPBatch
- [ ] Excitation upload path — CPU audio_in → SSBO per frame
- [ ] State persistence — mode y1/y2 state lives in GPU SSBO across frames
- [ ] Coefficient hot-update — only re-upload on parameter change
- [ ] Crossover logic — run GPU only when active voice count exceeds threshold
- [ ] Benchmark: CPU vs GPU at 8, 16, 32, 64 voices × 64 modes

### Phase GPU-3: GPU FFT + PhaseVocoder (3 weeks)

- [ ] Stockham radix-2 FFT compute shader (power-of-2 sizes: 256, 512, 1024, 2048, 4096)
- [ ] `fft_forward.glsl`, `fft_inverse.glsl`
- [ ] Phase vocoder analysis/synthesis shaders (per-bin parallel)
- [ ] `SymphonyPhaseVocoderGPU` proxy operator
- [ ] Multi-stream batching — multiple PV instances in one dispatch
- [ ] Benchmark: CPU PFFFT vs GPU FFT at various sizes and stream counts

### Phase GPU-4: GrainCloud + FDN (2 weeks)

- [ ] `grain_sum.glsl` — parallel grain windowing + accumulation
- [ ] PCM source upload to GPU texture/SSBO
- [ ] `SymphonyGrainCloudGPU` proxy operator
- [ ] `fdn_reverb.glsl` — parallel delay line processing
- [ ] `SymphonyFDNReverbGPU` proxy operator

### Phase GPU-5: Integration & Profiling (1 week)

- [ ] GPU performance monitor (exposed to Godot's Performance singleton)
- [ ] Editor debug overlay: GPU vs CPU execution time per operator type
- [ ] Automatic crossover detection — profile at startup, set thresholds
- [ ] Documentation and examples

---

## 8. File Structure

```
modules/symphony/
├── gpu/
│   ├── symphony_gpu_batch.h/.cpp        — Singleton batch manager
│   ├── symphony_gpu_thread.h/.cpp       — Dedicated compute thread
│   ├── symphony_gpu_triple_buffer.h     — Lock-free triple buffer
│   ├── symphony_gpu_capability.h/.cpp   — Device capability query
│   ├── proxies/
│   │   ├── symphony_modal_bank_gpu.h    — GPU proxy for ModalBank
│   │   ├── symphony_phase_vocoder_gpu.h — GPU proxy for PhaseVocoder
│   │   ├── symphony_grain_cloud_gpu.h   — GPU proxy for GrainCloud
│   │   └── symphony_fdn_reverb_gpu.h    — GPU proxy for FDN Reverb
│   └── shaders/
│       ├── modal_bank.glsl              — Batched modal synthesis
│       ├── fft_forward.glsl             — Stockham radix-2 FFT
│       ├── fft_inverse.glsl             — Inverse FFT
│       ├── phase_vocoder.glsl           — Per-bin STFT processing
│       ├── grain_sum.glsl               — Parallel grain windowing
│       └── fdn_reverb.glsl              — Parallel FDN processing
```

---

## 9. Latency Budget

| Component | Latency |
|-----------|---------|
| Triple buffer (2 blocks behind) | 2 × 64 = 128 samples = 2.67ms @ 48kHz |
| GPU dispatch + execution | ~0.5-1ms typical |
| Total added latency | ~3-4ms |
| Acceptable for | Ambience, impacts, reverb, music |
| NOT acceptable for | Real-time instrument with < 5ms requirement |

**Mitigation:** Operators where latency matters (live input response) stay on CPU. GPU backend is opt-in per operator type via the graph description.

---

## 10. Research References (from audio-books KB)

| Source | Key Contribution |
|--------|-----------------|
| ZhangYP05 — "Physically-based Sound Synthesis on GPUs" | GPU modal synthesis: store params in texture, per-mode parallel computation, recursive biquad response on fragment programs. Proves feasibility of real-time GPU modal with hundreds of modes. |
| Thesis (ARD) — Nikunj Raghuvanshi | GPU FFT gives 10× speedup over FFTW. Triple-buffer approach for acoustic simulation. Demonstrates GPU numerical acoustic simulation on complex scenes. |
| Game Audio Programming 2, Ch.3 — Multithreading | Ring buffer, double/triple buffer, spin lock, event-driven audio thread patterns. Direct application to our GPU↔audio sync. |
| Game Audio Programming 5, Ch.11 — Async Engine | One-way pipeline, steady-state observation. Validates separating compute from audio callback. |
| Computer Music Tutorial — Fast Convolution | GPUs for real-time convolution (Savioja, Välimäki, and Smith 2011). Block-transform convolution on GPU. |

---

## 11. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| GPU readback latency too high | Audible delay on percussive sounds | Keep percussive/reactive operators on CPU; GPU for sustained/ambient |
| Local RD not available on some platforms | Feature disabled | Transparent CPU fallback — no user-facing degradation |
| GPU contention with rendering | Frame drops | Use local RD (separate command stream), profile GPU utilization |
| Dispatch overhead exceeds compute savings for small batches | Wasted GPU time | Crossover threshold — only dispatch when voice count exceeds minimum |
| IIR state divergence between GPU/CPU paths | Audible glitch on fallback switch | State export/import between GPU SSBO and CPU arena on transition |
| Stockham FFT precision vs PFFFT | Subtle spectral artifacts | Validate with bit-exact comparison tests |

---

## 12. Success Metrics

- **CPU reduction:** ≥60% CPU savings on ModalBank workload at 20+ concurrent voices
- **GPU utilization:** <2ms GPU time per audio frame (leaves headroom for rendering)
- **Latency:** ≤4ms added latency (triple buffer at 64-sample blocks)
- **Fallback:** Zero audible difference between GPU and CPU paths
- **Crossover:** Automatic detection with <1 frame measurement overhead
