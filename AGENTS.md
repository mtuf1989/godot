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
| **S1** | Core Completion — DelayLine, FeedbackPath, SVFilter, ParameterSmoother, StochasticTrigger, PolyBLEP | `modules/symphony/` | — |
| **S2** | Synthesis Toolkit — ModalBank, GrainCloud, PitchShifter, Waveshaper, FM, CrossFade | `modules/symphony/` | — |
| **S3** | Advanced Synthesis — FormantOsc, FDN Reverb, Waveguide presets, LOD graphs | `modules/symphony/` | — |
| **S4** | Spectral — PhaseVocoder, SpectralGate, enhanced GrainCloud | `modules/symphony/` | — |
| **G1** | Foundation — SoundEvent, VoiceManager, EventDispatcher, AudioManager autoload | both | — |
| **G2** | Music System — MusicStateGraph, BeatClock, layer control | both | — |
| **G3** | RTPC Integration — parameter smoothing, curve evaluation, Symphony↔file-based unification | both | — |
| **G4** | Dialogue & Bus — DialogueAudioPipeline, BusController snapshots, auto-ducking | both | — |
| **G5** | Ambient & Spatial — AudioZone2D, scatter layers, distance attenuation | `game-template/` | — |
| **G6** | Profiling & Debug — performance monitors, debug overlay | `game-template/` | — |

**v1.0 milestone**: G1 + S1 + G2 + G3 (~55 days)

## Required Skills

The following skills MUST always be activated:

- `godot-gdscript` — Typed GDScript, signals, lifecycle callbacks, Godot 4 conventions.
- `godot-gdextension-cpp` — GDExtension C++, godot-cpp, ClassDB registration, GDCLASS macro, ownership discipline.

## Key Design Decisions

1. **C++ module** (not GDExtension) — direct access to AudioServer internals and audio thread.
2. **Arena allocation** — compiled graphs use a pre-allocated arena. Zero `new`/`delete` on audio thread.
3. **Micro-block processing** — 32 or 64 samples per execution step.
4. **Lock-free game→audio communication** — command ring buffer for thread safety.
5. **LLM-agent-first authoring** — all config is `.tres` files with deterministic, text-serializable format.
6. **Phase separation** — Symphony C++ phases (S1-S4) never mix with Game Audio Layer phases (G1-G6).

## Workflow for Picking Up Tasks

1. Read **`modules/symphony/dev-log.md`** for gotchas and pitfalls from prior sessions.
2. Search **audio-plan** KB for the task breakdown and acceptance criteria.
3. Search **audio-research** KB for algorithms, patterns, and Godot engine internals.
4. Search **audio-books** KB for DSP theory (Puckette, Perry Cook, GAP 1-5).
5. Search **godot-doc** KB for Godot class API references.
6. Read existing code in the referenced files before writing.
7. Implement following the acceptance criteria exactly.
8. One task = one commit.

## Knowledge Base Quick Reference

| KB Name | Contains | Use For |
|---------|----------|---------|
| `audio-plan` | Design doc + task breakdown | Architecture decisions, task specs, acceptance criteria |
| `audio-research` | Deep research syntheses | Voice management, beat sync, RTPC, spatial audio, adaptive music patterns |
| `audio-books` | Puckette, Perry Cook, GAP 1-5 | DSP algorithms, filter math, synthesis techniques, physical modeling |
| `godot-doc` | Godot XML class docs | API signatures, class inheritance, method parameters |
