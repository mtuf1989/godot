The file might be too large for grep to handle with the include pattern. Let me read the key files directly.

Now let me read the Controller struct to understand how it processes commands and runs the update loop:

Now let me look at the `Gen` trait to understand if there's any state transfer mechanism:

Now let me check the run_graph.rs for the RunGraph struct and how it processes blocks:

Now let me look at the node.rs to understand the Node struct:

Now I have a comprehensive understanding of the system. Let me provide the final response with the key file sections.

Now let me look at a few more specific details to round out the picture — particularly the `free_node` path and how the `TaskData` applied flag gates deallocation:

Now I have a complete picture. Let me read one more piece — the `GraphGen` struct definition to confirm the full data flow:

Now I have a thorough understanding of the system. Here's the complete analysis:

---

## Lock-Free Graph Mutation with "Free Graph" Acknowledgment Protocol in Knyst

### Architecture Overview

Knyst uses a **three-tier architecture** with strict separation between user threads, a controller thread, and the real-time audio thread:

```
User threads ──crossbeam channel──▶ Controller ──rtrb ring buffers──▶ Audio thread (GraphGen)
                                                  ◀──rtrb ring buffers──
```

### 1. Command Dispatch (Non-RT → Controller)

User-facing code sends `Command` variants through a `crossbeam_channel::Sender<Command>`:

```rust
enum Command {
    Push { gen_or_graph, node_address, graph_id, start_time },
    Connect(Connection),
    Disconnect(Connection),
    FreeNode(NodeId),
    FreeNodeMendConnections(NodeId),
    ScheduleChange(ParameterChange),
    FreeDisconnectedNodes,
    // ...
}
```

The `Controller` receives these on its own thread and applies them to the `Graph` data structure. This is **not** the audio thread — it's a separate non-RT thread that owns the `Graph`.

### 2. Lock-Free Task Delivery (Controller → Audio Thread)

When the graph topology changes (node added/removed, connection changed), the `Graph` sets `recalculation_required = true`. On the next `commit_changes()` call:

1. **Topological sort** — `calculate_node_order()` runs a depth-first traversal
2. **Task generation** — `generate_tasks()` creates a `Box<[Task]>` with raw pointers into the existing `Node` allocations
3. **Atomic delivery** — the new `TaskData` is pushed through an `rtrb::RingBuffer<TaskData>`

```rust
struct TaskData {
    applied: Arc<AtomicBool>,        // ← the acknowledgment flag
    tasks: Box<[Task]>,
    output_tasks: Box<[OutputTask]>,
    input_to_output_tasks: Box<[InputToOutputTask]>,
    new_inputs_buffers_ptr: Option<Arc<OwnedRawBuffer>>,
}
```

On the audio thread, `GraphGen::process()` checks for new task data each block:

```rust
let num_new_task_data = self.new_task_data_consumer.slots();
if num_new_task_data > 0 {
    for td in td_chunk {
        td.applied.store(true, Ordering::SeqCst);  // ACK!
        let old_td = std::mem::replace(&mut self.current_task_data, td);
        self.task_data_to_be_dropped_producer.push(old_td).ok();
    }
}
```

### 3. The Acknowledgment Protocol (Audio Thread → Controller)

This is the critical safety mechanism. It uses **two complementary strategies**:

**Strategy A: `Arc<AtomicBool>` change flags**

When a node is freed, the `Graph` stores the *current* `next_change_flag` alongside the node key:

```rust
// In free_node_from_key():
self.node_keys_to_free_when_safe
    .push((node_key, ggc.next_change_flag.clone()));
```

When `send_updated_tasks()` is called, it rotates the flag:

```rust
fn send_updated_tasks(&mut self, ...) {
    let current_change_flag = mem::replace(
        &mut self.next_change_flag,
        Arc::new(AtomicBool::new(false))
    );
    let td = TaskData { applied: current_change_flag, ... };
    self.new_task_data_producer.push(td);
}
```

The audio thread sets `applied = true` when it receives the new TaskData. Back on the controller thread, `free_old()` checks:

```rust
fn free_old(&mut self) {
    while i < self.node_keys_to_free_when_safe.len() {
        let (key, flag) = &self.node_keys_to_free_when_safe[i];
        if flag.load(Ordering::SeqCst) {
            // Audio thread confirmed it's using new tasks — safe to drop the node
            nodes.remove(*key);
            self.graphs_per_node.remove(*key);
        }
    }
}
```

**Strategy B: Ring buffer for old TaskData**

The audio thread pushes replaced `TaskData` back through `task_data_to_be_dropped_producer`. The controller's `GraphGenCommunicator::free_old()` drains and drops these on the non-RT side.

**Strategy C: `Arc::strong_count` for buffers**

Old input buffers are kept in `buffers_to_free_when_safe`. They're only dropped when `Arc::strong_count == 1`, meaning the audio thread no longer holds a reference.

### 4. State Preservation During Recompilation

This is the key insight for your research question: **Knyst preserves Gen state implicitly because it never moves or recreates Gen objects during graph recompilation.**

- `Node` stores its `Gen` as a raw pointer: `gen: *mut (dyn Gen + Send)` — functionally pinned
- `Task` contains raw pointers *into* existing `Node` allocations
- When the graph is recompiled, new `Task` structs are generated pointing to the **same** `Gen` objects
- The `Arc<UnsafeCell<SlotMap<NodeKey, Node>>>` is shared between `Graph` and `GraphGen`, ensuring allocations survive even if `Graph` is dropped

So when you add a connection, remove a node, or restructure the graph:
- Oscillator phases → **preserved** (same Gen object, same memory)
- Filter histories → **preserved**
- Delay line contents → **preserved**

### 5. When State IS Lost

State is only lost when a node is explicitly **freed** (`FreeNode`, `FreeSelf`, etc.) and a new node is pushed. There is **no mechanism** to transfer internal Gen state from one node to another — the `Gen` trait has no serialization or state-extraction methods.

### 6. Node Self-Freeing (Audio Thread → Controller)

Nodes can signal their own removal from the audio thread:

```rust
match task.run(...) {
    GenState::FreeSelf => {
        self.free_node_queue_producer.push((task.node_key, GenState::FreeSelf)).ok();
    }
    GenState::FreeGraph(from_sample_nr) => {
        self.graph_state = GenState::FreeSelf;
        do_empty_buffer = Some(from_sample_nr);
    }
    // ...
}
```

The controller drains this queue in `free_old()` and processes the removals.

### Summary Table

| Mechanism | Direction | Channel Type | Purpose |
|-----------|-----------|-------------|---------|
| `new_task_data` | Controller → Audio | `rtrb::RingBuffer<TaskData>` | Deliver recompiled graph |
| `applied` flag | Audio → Controller | `Arc<AtomicBool>` | Confirm new tasks received |
| `task_data_to_be_dropped` | Audio → Controller | `rtrb::RingBuffer<TaskData>` | Return old tasks for deallocation |
| `free_node_queue` | Audio → Controller | `rtrb::RingBuffer<(NodeKey, GenState)>` | Nodes requesting self-removal |
| `scheduled_change` | Controller → Audio | `rtrb::RingBuffer<ScheduledChange>` | Sample-accurate parameter changes |
| `clock_update` | Controller → Audio | `rtrb::RingBuffer<ClockUpdate>` | Clock synchronization |
| `Arc<UnsafeCell<SlotMap>>` | Shared | Arc (ownership only) | Keep node allocations alive |

### Implications for Hot-Swapping Graphs

If you wanted to implement full graph hot-swap with state inheritance, you'd need to extend the `Gen` trait with something like:

```rust
trait Gen {
    fn serialize_state(&self) -> Option<Box<dyn Any>> { None }
    fn restore_state(&mut self, state: Box<dyn Any>) {}
}
```

Currently, the architecture avoids this need by keeping Gen objects alive across recompilations — the "state transfer" is implicit because the objects are never replaced, only the execution order and wiring around them changes.