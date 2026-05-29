The AudioProcessorGraph files are in the `juce_audio_processors_headless` module. Let me read both the header and implementation files.

Now let me read the implementation file which will contain the critical details about node management, rendering sequence, and synchronization:

Now I have a complete picture of the implementation. Let me provide the final response with the key file sections.

Now let me look at the `NodeStates` class to understand how it tracks which nodes need preparation, and the `RenderSequenceBuilder` to understand how the execution plan is constructed:

Now let me look at the `RenderSequenceExchange` timer callback and how the old sequence is cleaned up, plus the `processBlock` flow more closely:

Now I have a complete picture. Let me also check how the delay compensation lines in the render sequence are handled (since those are part of the "graph's internal state" that gets rebuilt):

Now I have a thorough understanding of the entire mechanism. Here's my analysis:

---

## Deep Dive: `AudioProcessorGraph` — Dynamic Node Addition/Removal During Playback

### Architecture Overview

The `AudioProcessorGraph` uses a **two-layer architecture** to handle live topology changes:

1. **Topology layer** (main thread): `Nodes`, `Connections`, `NodeStates` — the mutable graph structure
2. **Execution layer** (audio thread): `RenderSequence` — an immutable, pre-baked execution plan

These are bridged by `RenderSequenceExchange`, a wait-free pointer-swap mechanism.

---

### How State Is Preserved (The Key Insight)

**The `AudioProcessor` instances themselves are never destroyed or recreated during a graph rebuild.** When you add/remove nodes or connections:

1. The `Node::Ptr` (reference-counted) objects persist in the `Nodes` collection
2. Only the `RenderSequence` (the execution plan) is rebuilt
3. The new `RenderSequence` holds `Node::Ptr` references to the **same processor instances**
4. `prepareToPlay` is only called on **newly added** nodes (tracked via `preparedNodes` set in `NodeStates`)

This means oscillator phases, filter histories, and any internal processor state survive a graph reconfiguration automatically — the processors keep running with their accumulated state.

---

### The Rebuild Pipeline

When topology changes (e.g., `addNode`, `removeNode`, `addConnection`):

```
topologyChanged(kind)
  → rebuild(kind)
    → handleAsyncUpdate()  [main thread]
      → nodeStates.applySettings(nodes)     // prepares only NEW nodes
      → RenderSequenceSignature check       // skip rebuild if nothing changed
      → new RenderSequence(settings, nodes, connections)  // builds new execution plan
      → renderSequenceExchange.set(sequence) // stages for audio thread pickup
```

---

### Wait-Free Audio Thread Exchange (`RenderSequenceExchange`)

```cpp
class RenderSequenceExchange final : private Timer {
    SpinLock mutex;
    std::unique_ptr<RenderSequence> mainThreadState, audioThreadState;
    bool isNew = false;
};
```

- **`set()` (main thread):** Takes the SpinLock, stores the new sequence, sets `isNew = true`
- **`updateAudioThreadState()` (audio thread):** Uses `ScopedTryLockType` — if the lock is free, **swaps pointers** (not copies). If the lock is held, the audio thread continues with the old sequence. Zero blocking.
- **Timer (500ms):** Cleans up the old sequence on the main thread after the audio thread has picked up the new one

The pointer swap is critical: the old `RenderSequence` is never `delete`d on the audio thread.

---

### What IS Lost During a Rebuild

While processor state is preserved, the **graph's internal delay compensation lines** are NOT carried over:

```cpp
struct DelayChannelOp final : public RenderOp {
    std::vector<FloatType> buffer;  // freshly allocated, zeroed
    int readIndex = 0, writeIndex;
};
```

Each `RenderSequence` creates new `DelayChannelOp` instances with zeroed buffers. If the graph topology changes and latency compensation delays are recalculated, the delay line contents are lost. This could cause a brief discontinuity in the compensated signal paths.

---

### `NodeStates::applySettings` — The Preparation Logic

```cpp
std::optional<PrepareSettings> applySettings(const Nodes& n) {
    if (settingsChanged) {
        // Settings changed → release ALL nodes, re-prepare ALL
        for (node : nodes) node->getProcessor()->releaseResources();
        preparedNodes.clear();
    }

    if (current.has_value()) {
        for (node : nodes) {
            if (already prepared) continue;  // ← existing nodes skip this
            preparedNodes.insert(node->nodeID);
            node->getProcessor()->prepareToPlay(sampleRate, blockSize);
        }
    }
}
```

Key behaviors:
- **Same settings, existing node:** No `prepareToPlay` call → state preserved
- **Same settings, new node:** `prepareToPlay` called → fresh state
- **Settings changed (sample rate/block size):** ALL nodes get `releaseResources()` + `prepareToPlay()` → state reset

---

### Threading Safety

The implementation comment at the top of the file explains the CoreAudio deadlock avoidance:

> `prepareToPlay` on nodes is always called from the main thread WITHOUT the graph's callback lock held. The render sequence settings check in `processBlock` ensures no node is processed while a rebuild is in progress.

The safety guarantee: if the audio thread sees a `RenderSequence` whose `PrepareSettings` don't match the current requested settings, it clears the output and does nothing — preventing any race between `prepareToPlay` and `processBlock` on the same processor.

---

### Summary Table

| Aspect | Preserved? | Mechanism |
|--------|-----------|-----------|
| Processor internal state (phases, filters) | ✅ Yes | Same `AudioProcessor*` instances reused |
| Delay line contents (graph-level compensation) | ❌ No | New `DelayChannelOp` with zeroed buffers |
| Node preparation state | ✅ Yes | `preparedNodes` set tracks what's already prepared |
| Audio continuity during swap | ✅ Yes | Wait-free try-lock; old sequence continues if new isn't ready |
| State during sample rate change | ❌ No | All nodes get `releaseResources()` + `prepareToPlay()` |

---

### Implications for "Hot-Recompile" Scenarios

If you're building an editor that recompiles a graph while playing:

1. **Keep the same `AudioProcessor` instances** for nodes that haven't changed — the graph will preserve their state automatically
2. **Only add/remove the nodes that actually changed** — don't rebuild from scratch
3. **Use `UpdateKind::async`** for batched changes to avoid multiple rebuilds
4. **Be aware that delay compensation lines reset** — if your graph has latency-compensated parallel paths, there will be a brief transient when the new sequence activates
5. **The swap is non-blocking** — the audio thread will seamlessly transition to the new execution plan at the next buffer boundary, with no gap or glitch (aside from the delay line reset)