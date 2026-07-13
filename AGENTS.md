# Symphony Audio System — Agent Guide

## What This Project Is

This is the **Symphony module** (`modules/symphony/`) — a C++ audio engine built as a Godot module. It provides:

- **DSP Graph Engine**: Arena-allocated, audio-thread-only compiled graphs that execute per micro-block (32/64 samples). Zero allocation during playback.
- **Runtime Services**: VoiceManager, EventDispatcher, RTPCEngine, BeatClock, BusController — all C++ singletons exposed to GDScript.
- **Resource Types**: SoundEvent, MusicStateGraph, AudioStreamSymphony — `.tres` files that are the source of truth.

Symphony is the **low-level engine layer**. It does NOT contain game-specific logic. The Game Audio Layer (GDScript autoloads in `game-template/` repo) sits on top.

## Architecture (Two Repos, One System)

```
THIS REPO: godot/modules/symphony/
├── core/          — Graph compiler, arena allocator, operator registry
├── nodes/         — DSP operators (Oscillator, Filter, Delay, ADSR, etc.)
├── runtime/       — VoiceManager, EventDispatcher, BeatClock, BusController, SoundEvent, RTPCEngine
└── register_types.cpp

SEPARATE REPO: game-template/
├── addons/symphony_audio/  — GDScript autoloads (AudioManager, MusicSystem, AmbientSystem, DialogueAudioPipeline)
└── audio/                  — .tres resources (events, graphs, banks, config)
```

Dependency direction: Game Audio Layer → Symphony. Never the reverse.

## Implementation Phases

| Phase | Scope | Repo | Status |
|-------|-------|------|--------|
| **S1** | Core Completion — DelayLine, FeedbackPath, SVFilter, ParameterSmoother, StochasticTrigger, PolyBLEP | `modules/symphony/` | Done |
| **S2** | Synthesis Toolkit — ModalBank, GrainCloud, PitchShifter, Waveshaper, FM, CrossFade | `modules/symphony/` | Done |
| **S3** | Advanced Synthesis — FormantOsc, FDN Reverb, Waveguide presets, LOD graphs | `modules/symphony/` | Done |
| **S4** | Spectral Done PhaseVocoder, SpectralGate, enhanced GrainCloud | `modules/symphony/` | Done |
| **G1** | Foundation — SoundEvent, VoiceManager, EventDispatcher, AudioManager autoload | both | Done |
| **G2** | Music System — MusicStateGraph, BeatClock, layer control | both | Done |
| **G3** | RTPC Integration — parameter smoothing, curve evaluation, Symphony↔file-based unification | both | Done |
| **G4** | Dialogue & Bus — DialogueAudioPipeline, BusController snapshots, auto-ducking | both | Done |
| **G5** | Ambient & Spatial — AudioZone2D, scatter layers, distance attenuation | `game-template/` | Done |
| **G6** | Profiling & Debug — performance monitors, debug overlay | `game-template/` | Done |

**v1.0 milestone**: Done

## Required Skills

The following skills MUST always be activated:

- `godot-gdscript` — Typed GDScript, signals, lifecycle callbacks, Godot 4 conventions.
- `godot-gdextension-cpp` — GDExtension C++, godot-cpp, ClassDB registration, GDCLASS macro, ownership discipline.

## Knowledge Base Tool

Always using Knowledge Base Tool to verify solutions or answers related to symphony module, procedural sounds and musics, techniques and optimizations

- `audio-books` - this knowledge contains many books + researches involving how to create procedural sounds & musics
- `stk-code` - Code for reference. This library framework details how to design object-oriented C++ classes for foundational audio building blocks (such as delay lines, filters, envelopes, and physical modeling nodes). It explains the software engineering principles required for real-time safe audio processing, including lock-free ring buffers, sample-accurate parameter interpolation, and memory pre-allocation to prevent runtime thread stalls. The library structure serves as a template for combining modular unit generators into complex physical configurations.
- `faust-code` - Code for reference. This language reference demonstrates how to define block-diagram equations using a functional programming syntax optimized for signal processing. The Faust compiler analyzes these functional representations to perform compiler optimization, automatic loop unrolling, and SIMD (Single Instruction Multiple Data) vectorization. It teaches how to construct complex networks of waveguides and modal biquads that compile natively to run inside game audio engines (e.g., Wwise, FMOD, Unreal Engine) with minimal overhead.

## Key Design Decisions

1. **C++ module** (not GDExtension) — direct access to AudioServer internals and audio thread.
2. **Arena allocation** — compiled graphs use a pre-allocated arena. Zero `new`/`delete` on audio thread.
3. **Micro-block processing** — 32 or 64 samples per execution step.
4. **Lock-free game→audio communication** — command ring buffer for thread safety.
5. **LLM-agent-first authoring** — all config is `.tres` files with deterministic, text-serializable format.
6. **Phase separation** — Symphony C++ phases (S1-S4) never mix with Game Audio Layer phases (G1-G6).
