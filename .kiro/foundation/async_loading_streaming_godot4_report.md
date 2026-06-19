# Async Loading & Runtime Streaming in Godot 4.x — Technical Report
## GDScript Focus | March 2026

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Core Systems: ResourceLoader Threaded API](#2-core-systems-resourceloader-threaded-api)
3. [Scene Streaming Strategies](#3-scene-streaming-strategies)
4. [Performance & UX Impact: Sources of Frame Hitching](#4-performance--ux-impact-sources-of-frame-hitching)
5. [Critical Bottlenecks](#5-critical-bottlenecks)
6. [Architecture Patterns](#6-architecture-patterns)
7. [Failure Cases](#7-failure-cases)
8. [Benchmarks & Measured Behavior](#8-benchmarks--measured-behavior)
9. [Decision Rules](#9-decision-rules)
10. [Tradeoff Tables](#10-tradeoff-tables)
11. [Sources](#11-sources)

---

## 1. Executive Summary

Godot 4.x provides `ResourceLoader.load_threaded_request()` as its primary mechanism for background resource loading. While this eliminates the main-thread stall caused by `load()`, it does **not** solve all sources of frame hitching. Three distinct phases produce stutter independently:

1. **Disk I/O + parsing** (solved by threaded loading)
2. **PackedScene.instantiate()** (typically run on main thread due to `add_child()` constraint; 40–60ms per complex scene)
3. **GPU upload: shader compilation + texture/mesh transfer** (first-draw stutter)

A production-grade streaming system must address all three. This report provides the technical depth, measured behaviors, architecture patterns, and decision rules to do so.

---

## 2. Core Systems: ResourceLoader Threaded API

### 2.1 API Lifecycle

The threaded loading API consists of three methods forming a request → poll → retrieve cycle:

```
┌─────────────────────────────────────────────────────────────────┐
│                    THREADED LOADING LIFECYCLE                     │
│                                                                   │
│  ┌──────────────────┐    ┌──────────────────┐    ┌─────────────┐ │
│  │ load_threaded_    │───▶│ load_threaded_    │───▶│ load_       │ │
│  │ request(path)     │    │ get_status(path)  │    │ threaded_   │ │
│  │                   │    │                   │    │ get(path)   │ │
│  │ Starts background │    │ Poll each frame   │    │ Retrieve    │ │
│  │ thread loading    │    │ in _process()     │    │ resource    │ │
│  └──────────────────┘    └──────────────────┘    └─────────────┘ │
│                                                                   │
│  Status values:                                                   │
│  THREAD_LOAD_INVALID_RESOURCE (0) - Not requested / invalid      │
│  THREAD_LOAD_IN_PROGRESS      (1) - Still loading                │
│  THREAD_LOAD_FAILED           (2) - Error occurred               │
│  THREAD_LOAD_LOADED           (3) - Ready to retrieve            │
└─────────────────────────────────────────────────────────────────┘
```

#### GDScript — Canonical Pattern

```gdscript
const SCENE_PATH := "res://levels/world_02.tscn"

func _ready():
    ResourceLoader.load_threaded_request(SCENE_PATH, "", false, ResourceLoader.CACHE_MODE_REUSE)

func _process(_delta):
    var progress := []
    var status := ResourceLoader.load_threaded_get_status(SCENE_PATH, progress)
    
    match status:
        ResourceLoader.THREAD_LOAD_IN_PROGRESS:
            # progress[0] is 0.0 → 1.0
            update_progress_bar(progress[0])
        ResourceLoader.THREAD_LOAD_LOADED:
            var scene: PackedScene = ResourceLoader.load_threaded_get(SCENE_PATH)
            _transition_to(scene)
            set_process(false)
        ResourceLoader.THREAD_LOAD_FAILED:
            push_error("Failed to load: %s" % SCENE_PATH)
            set_process(false)
```

### 2.2 Parameters Deep Dive

| Parameter | Values | Behavior | Risk |
|-----------|--------|----------|------|
| `use_sub_threads` | `false` (default) | Single background thread loads resource sequentially | Slower but no main-thread contention |
| `use_sub_threads` | `true` | Distributes sub-resource loading across the `WorkerThreadPool` (thread count determined by pool configuration, not strictly 1 per core) | **Can cause main-thread slowdowns** due to CPU scheduling contention. Known to exhaust thread pool on web exports ([#74051](https://github.com/godotengine/godot/issues/74051)) |
| `cache_mode` | `CACHE_MODE_REUSE` (default) | Returns cached resource if already loaded | Safe default |
| `cache_mode` | `CACHE_MODE_IGNORE` | Bypasses cache entirely | Can cause null resources or infinite loops ([#111385](https://github.com/godotengine/godot/issues/111385)) |
| `cache_mode` | `CACHE_MODE_REPLACE` | Refreshes cached data from disk | Useful for hot-reload scenarios |

### 2.3 Thread Safety Constraints

Godot's scene tree is **not thread-safe**. Critical rules:

- **Never** call `add_child()`, `remove_child()`, or modify the scene tree from a background thread
- `ResourceLoader.load_threaded_request()` handles its own threading internally — you don't manage threads
- `PackedScene.instantiate()` can technically run off-thread, but `add_child()` requires the main thread — so tree insertion must be deferred
- Use `call_deferred()` when adding loaded resources to the tree from callbacks
- `load_threaded_get()` blocks the calling thread if loading isn't complete — always check status first
- Calling `load_threaded_request()` twice with the same path is handled gracefully — the engine reuses the existing load token and increments a user reference count (returns `OK`, not an error). However, checking status before re-requesting is still good practice to avoid unnecessary overhead

### 2.4 Polling vs Blocking

| Approach | Code | Frame Impact |
|----------|------|-------------|
| **Polling** (recommended) | Check `load_threaded_get_status()` in `_process()` | Zero frame impact; game continues running |
| **Blocking** | Call `load_threaded_get()` before load completes | Freezes calling thread until done — equivalent to `load()` |
| **Spin-lock** (anti-pattern) | `while` loop checking status | Freezes main thread entirely |

**Decision rule**: Always poll in `_process()`. Never use while-loops. If you need the resource immediately, accept the blocking cost of `load_threaded_get()` but understand it defeats the purpose.

---

## 3. Scene Streaming Strategies

### 3.1 Strategy Comparison Matrix

| Strategy | Description | Memory | Latency | Complexity | Best For |
|----------|-------------|--------|---------|------------|----------|
| **Full scene swap** | `change_scene_to_packed()` replaces entire tree | Low (one scene at a time) | High (full load + instantiate) | Low | Linear level games, menu transitions |
| **Additive loading** | Multiple scenes as children of root | Medium-High | Low (preloaded) | Medium | Hub worlds, layered UI, dungeons |
| **Chunk-based streaming** | Grid of scene chunks loaded/unloaded by proximity | Controlled (N×N grid) | Medium (async + instantiate) | High | Open worlds, large terrains |
| **Preload everything** | `preload()` all scenes at script parse time | Very High | Zero at runtime | Low | Small games, jam projects |
| **On-demand loading** | `load()` when needed | Low | High (blocking) | Low | Rarely accessed content |

### 3.2 Full Scene Loading

The simplest approach. Godot's `SceneTree.change_scene_to_packed()` frees the current scene and instantiates the new one.

```gdscript
# Loading screen autoload pattern
# Register this as an Autoload so it survives scene changes
extends CanvasLayer

@onready var progress_bar: ProgressBar = $ProgressBar
var _target_path: String

func transition_to(scene_path: String):
    _target_path = scene_path
    show()
    ResourceLoader.load_threaded_request(scene_path)
    set_process(true)

func _process(_delta):
    var progress := []
    var status := ResourceLoader.load_threaded_get_status(_target_path, progress)
    match status:
        ResourceLoader.THREAD_LOAD_IN_PROGRESS:
            progress_bar.value = progress[0] * 100.0
        ResourceLoader.THREAD_LOAD_LOADED:
            var scene := ResourceLoader.load_threaded_get(_target_path)
            get_tree().change_scene_to_packed(scene)
            set_process(false)
            hide()
```

**Limitation**: `change_scene_to_packed()` calls `instantiate()` and `add_child()` on the main thread, which can cause a hitch for complex scenes.

### 3.3 Additive Scene Loading (Multiple Active Scenes)

Instead of replacing the scene tree, add/remove scene branches as children:

```gdscript
# WorldManager.gd — Autoload
var _loaded_zones: Dictionary = {}  # path -> Node

func load_zone(zone_path: String, parent: Node):
    if _loaded_zones.has(zone_path):
        return  # Already loaded
    
    ResourceLoader.load_threaded_request(zone_path)
    _loaded_zones[zone_path] = null  # Mark as loading

func _process(_delta):
    for path in _loaded_zones.keys():
        if _loaded_zones[path] != null:
            continue  # Already instantiated
        
        var status := ResourceLoader.load_threaded_get_status(path)
        if status == ResourceLoader.THREAD_LOAD_LOADED:
            var scene: PackedScene = ResourceLoader.load_threaded_get(path)
            var instance := scene.instantiate()
            get_node("/root/World").add_child.call_deferred(instance)
            _loaded_zones[path] = instance

func unload_zone(zone_path: String):
    if _loaded_zones.has(zone_path):
        var node: Node = _loaded_zones[zone_path]
        if node:
            node.queue_free()
        _loaded_zones.erase(zone_path)
```

**Key insight**: Additive loading keeps persistent elements (player, UI, audio) alive across transitions. This is the foundation for seamless worlds.

### 3.4 Chunk-Based World Streaming

The standard open-world pattern. The world is divided into a grid; a manager loads/unloads chunks based on player position.

```
┌─────────────────────────────────────────────┐
│              CHUNK STREAMING MODEL            │
│                                               │
│   ┌───┬───┬───┬───┬───┐                     │
│   │   │ P │ P │ P │   │  P = Preloaded       │
│   ├───┼───┼───┼───┼───┤  A = Active          │
│   │ P │ A │ A │ A │ P │  (blank) = Unloaded  │
│   ├───┼───┼───┼───┼───┤                     │
│   │ P │ A │ @ │ A │ P │  @ = Player chunk    │
│   ├───┼───┼───┼───┼───┤                     │
│   │ P │ A │ A │ A │ P │                     │
│   ├───┼───┼───┼───┼───┤                     │
│   │   │ P │ P │ P │   │                     │
│   └───┴───┴───┴───┴───┘                     │
│                                               │
│   Active ring: instantiated + in scene tree   │
│   Preload ring: loaded in memory, not in tree │
│   Beyond: unloaded from memory                │
└─────────────────────────────────────────────┘
```

```gdscript
# ChunkManager.gd
extends Node

const CHUNK_SIZE := 64.0  # World units per chunk
const ACTIVE_RADIUS := 1   # Chunks around player that are in-tree
const PRELOAD_RADIUS := 2  # Chunks loaded in memory but not in-tree

var _chunks: Dictionary = {}       # Vector2i -> Node (or null if loading)
var _loaded_resources: Dictionary = {} # Vector2i -> PackedScene
var _player_chunk := Vector2i.ZERO

func _process(_delta):
    var new_chunk := _world_to_chunk(player.global_position)
    if new_chunk != _player_chunk:
        _player_chunk = new_chunk
        _update_chunks()
    _poll_loading()

func _world_to_chunk(pos: Vector3) -> Vector2i:
    return Vector2i(int(pos.x / CHUNK_SIZE), int(pos.z / CHUNK_SIZE))

func _update_chunks():
    var needed_chunks: Array[Vector2i] = []
    
    # Determine all chunks within preload radius
    for x in range(-PRELOAD_RADIUS, PRELOAD_RADIUS + 1):
        for z in range(-PRELOAD_RADIUS, PRELOAD_RADIUS + 1):
            needed_chunks.append(_player_chunk + Vector2i(x, z))
    
    # Unload chunks outside preload radius
    for coord in _chunks.keys():
        if coord not in needed_chunks:
            _unload_chunk(coord)
    
    # Request loading for needed chunks
    for coord in needed_chunks:
        if not _chunks.has(coord) and not _loaded_resources.has(coord):
            _request_chunk(coord)
    
    # Activate/deactivate based on active radius
    for coord in _chunks.keys():
        var dist := (_player_chunk - coord).length()
        var node: Node = _chunks[coord]
        if node:
            node.visible = dist <= ACTIVE_RADIUS
            node.process_mode = Node.PROCESS_MODE_INHERIT if dist <= ACTIVE_RADIUS \
                else Node.PROCESS_MODE_DISABLED

func _request_chunk(coord: Vector2i):
    var path := "res://chunks/chunk_%d_%d.tscn" % [coord.x, coord.y]
    if ResourceLoader.exists(path):
        ResourceLoader.load_threaded_request(path)
        _loaded_resources[coord] = null  # Mark as loading

func _poll_loading():
    for coord in _loaded_resources.keys():
        if _loaded_resources[coord] != null:
            continue
        var path := "res://chunks/chunk_%d_%d.tscn" % [coord.x, coord.y]
        var status := ResourceLoader.load_threaded_get_status(path)
        if status == ResourceLoader.THREAD_LOAD_LOADED:
            var scene: PackedScene = ResourceLoader.load_threaded_get(path)
            _loaded_resources[coord] = scene
            # Only instantiate if within active radius
            var dist := (_player_chunk - coord).length()
            if dist <= ACTIVE_RADIUS:
                _instantiate_chunk(coord, scene)

func _instantiate_chunk(coord: Vector2i, scene: PackedScene):
    var instance := scene.instantiate()
    instance.position = Vector3(coord.x * CHUNK_SIZE, 0, coord.y * CHUNK_SIZE)
    add_child.call_deferred(instance)
    _chunks[coord] = instance

func _unload_chunk(coord: Vector2i):
    if _chunks.has(coord):
        var node: Node = _chunks[coord]
        if node:
            node.queue_free()
        _chunks.erase(coord)
    _loaded_resources.erase(coord)
```

---

## 4. Performance & UX Impact: Sources of Frame Hitching

### 4.1 The Three Stutter Sources

```
┌─────────────────────────────────────────────────────────────────────┐
│                  THREE SOURCES OF FRAME HITCHING                     │
│                                                                       │
│  Phase 1: DISK I/O + PARSING                                        │
│  ├── Cause: Reading .tscn/.tres from disk, parsing resource format   │
│  ├── Duration: 50ms – 5000ms+ depending on scene complexity          │
│  ├── Thread: Background (if using load_threaded_request)             │
│  └── Solution: ✅ Solved by threaded loading                         │
│                                                                       │
│  Phase 2: INSTANTIATION                                              │
│  ├── Cause: PackedScene.instantiate() creates node tree              │
│  ├── Duration: 40–60ms per complex scene (measured, see #1801)       │
│  ├── Thread: MAIN THREAD ONLY (cannot be offloaded)                  │
│  └── Solution: ⚠️ Object pooling, pre-instantiation, staggering     │
│                                                                       │
│  Phase 3: GPU UPLOAD                                                 │
│  ├── Cause: Shader compilation + texture/mesh upload on first draw   │
│  ├── Duration: Variable, 16ms–200ms+ per unique shader variant       │
│  ├── Thread: RENDER THREAD (blocks frame presentation)               │
│  └── Solution: ⚠️ Shader warm-up, pre-rendering behind load screen  │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.2 Phase 1: Disk I/O (Solved)

`ResourceLoader.load_threaded_request()` moves all file reading and resource parsing to a background thread. The main thread continues rendering and processing input. This is the "easy win."

**Measured behavior**: A scene that takes 2 seconds to `load()` synchronously will take the same 2 seconds via threaded loading, but the game continues running at full framerate during that time.

### 4.3 Phase 2: Instantiation Spike (Partially Solved)

`PackedScene.instantiate()` itself has no thread guard and can technically run off the main thread; the constraint is that `add_child()` requires the main thread. In practice, instantiation is typically done on the main thread because tree insertion must follow immediately. For a scene with hundreds of nodes, this can take 40–60ms — enough to drop below 60fps for one or more frames.

**Evidence**: [Godot proposal #1801](https://github.com/godotengine/godot-proposals/issues/1801) documents 40–60ms instantiation hitches in an open-world chunk loader. The proposal requests an `instance_interactive()` method that doesn't exist yet.

**Community reports confirm**: "Threaded loading doesn't fully fix stutters when instantiating a scene. The real stutters come when you do `add_child` and `instantiate`." ([forum](https://forum.godotengine.org/t/how-to-lazy-load-a-scene/134201))

**Mitigation strategies**:

| Strategy | How | Tradeoff |
|----------|-----|----------|
| Object pooling | Pre-instantiate during load screen, reuse via show/hide | Higher baseline memory |
| Staggered instantiation | Instantiate 1–2 chunks per frame across multiple frames | Visible pop-in |
| Pre-instantiation ring | Instantiate in preload ring before player arrives | Requires prediction of movement |
| Simpler scenes | Reduce node count per chunk | Design constraint |

### 4.4 Phase 3: GPU Upload / Shader Compilation (Unsolved at Engine Level)

Godot 4.x compiles specialized shader pipelines on demand — the first time a material/shader combination is rendered with a specific set of renderer features. However, since Godot 4.4, an **ubershader fallback** (`ubershader=1`) renders objects with a pre-compiled generic shader while the specialized one compiles in the background, and `mesh_generate_pipelines()` supports background pre-compilation. The stutter is **independent of resource loading**.

**Evidence**: [Issue #61233](https://github.com/godotengine/godot/issues/61233) — confirmed regression from Godot 3.x where ubershaders prevented cold-cache stutter. Godot 4's Vulkan/Metal renderers do not yet have equivalent ubershader support.

**Shader cache behavior**:
- First run: shaders compile on-demand → stutter
- Subsequent runs: `.godot/shader_cache/` is used (or `user://shader_cache/` fallback when no project path is set) → no stutter
- Deleting shader cache → stutter returns
- Different GPU/driver → cache invalidated

**Workaround — Shader Warm-Up Pattern**:

```gdscript
# ShaderWarmup.gd — Run during loading screen
# Render every unique material for 1 frame behind an opaque overlay
extends Node

@export var materials_to_warm: Array[Material] = []

func warm_up():
    for mat in materials_to_warm:
        var mesh_instance := MeshInstance3D.new()
        mesh_instance.mesh = BoxMesh.new()
        mesh_instance.material_override = mat
        # Place off-screen or behind loading overlay
        mesh_instance.position = Vector3(0, -1000, 0)
        add_child(mesh_instance)
    
    # Wait one frame for rendering to trigger compilation
    await get_tree().process_frame
    
    # Clean up
    for child in get_children():
        if child is MeshInstance3D:
            child.queue_free()
```

For particle shaders, you must instantiate the actual `GPUParticles2D`/`GPUParticles3D` node and let it render at least one frame.

---

## 5. Critical Bottlenecks

### 5.1 PackedScene Instancing Cost

| Scene Complexity | Approximate instantiate() Time | Notes |
|-----------------|-------------------------------|-------|
| Simple (10–20 nodes) | < 1ms | Negligible |
| Medium (50–100 nodes) | 5–15ms | Noticeable at 60fps (16.6ms budget) |
| Complex (200+ nodes with physics) | 40–60ms | Guaranteed frame drop |
| Very complex (1000+ nodes) | 200ms+ | Multiple frame drops |

Source: Community measurements from [proposal #1801](https://github.com/godotengine/godot-proposals/issues/1801) and [forum reports](https://forum.godotengine.org/t/optimisation-issue-with-instantiation/79604).

### 5.2 Texture and Mesh Upload

Textures and meshes are uploaded to GPU VRAM on first use. Large textures (4K+) can cause a single-frame stutter. This is separate from shader compilation.

**Behavior**: Even if a resource is loaded in memory (RAM), the GPU upload happens when the renderer first draws it. There is no explicit API to force GPU upload in Godot 4.x.

**Mitigation**: Same as shader warm-up — render the object for one frame behind a loading overlay.

### 5.3 Memory Management: Reference Counting

Godot uses **reference counting** (not garbage collection) for `Resource` objects:

- Resources are freed immediately when their reference count drops to zero
- No GC pauses or spikes from collection cycles
- **Risk**: Holding references in dictionaries/arrays prevents freeing → memory bloat
- `WeakRef` allows soft caching without preventing cleanup

```gdscript
# Soft cache pattern — resources freed when no strong refs remain
var _cache: Dictionary = {}

func get_cached(path: String) -> Resource:
    if _cache.has(path):
        var ref: Resource = _cache[path].get_ref()
        if ref:
            return ref
    var res := load(path)
    _cache[path] = weakref(res)
    return res
```

**Node memory**: Nodes removed with `remove_child()` but not `queue_free()` remain in memory (this is by design — `remove_child()` is intentionally non-destructive to allow re-parenting). For Nodes (which are not RefCounted), unmanaged orphans are a real memory leak. Always pair removal with `queue_free()`, or explicitly manage pooled/re-parented nodes.

---

## 6. Architecture Patterns

### 6.1 Pattern: Loading Screen Transition

```
┌──────────────────────────────────────────────────────────────┐
│              LOADING SCREEN TRANSITION FLOW                    │
│                                                                │
│  Game Scene          Loading Screen (Autoload)    New Scene   │
│  ──────────          ────────────────────────     ─────────   │
│       │                                                        │
│       │──── request_transition("level2.tscn") ──▶│            │
│       │                                          │            │
│       │              show overlay                │            │
│       │              load_threaded_request()     │            │
│       │                                          │            │
│       │              _process: poll status       │            │
│       │              update progress bar         │            │
│       │              ....                        │            │
│       │                                          │            │
│       │              THREAD_LOAD_LOADED          │            │
│       │              load_threaded_get()         │            │
│       │              warm_up_shaders()           │            │
│       │              await process_frame         │            │
│       │                                          │            │
│       ×              change_scene_to_packed() ───────▶│      │
│   (freed)            hide overlay                     │      │
│                                                       │      │
│                                                  _ready()    │
└──────────────────────────────────────────────────────────────┘
```

**Key**: The loading screen must be an Autoload (singleton) so it survives `change_scene_to_packed()`.

### 6.2 Pattern: Seamless Open-World Streaming

```
┌──────────────────────────────────────────────────────────────┐
│            SEAMLESS STREAMING ARCHITECTURE                     │
│                                                                │
│  ┌─────────────┐     ┌──────────────┐     ┌───────────────┐  │
│  │ ChunkManager│────▶│ LoadQueue    │────▶│ ResourceLoader│  │
│  │ (Autoload)  │     │ (priority    │     │ (threaded)    │  │
│  │             │     │  sorted)     │     │               │  │
│  └──────┬──────┘     └──────────────┘     └───────────────┘  │
│         │                                                      │
│         │ polls status each frame                              │
│         │                                                      │
│         ▼                                                      │
│  ┌─────────────┐     ┌──────────────┐                         │
│  │ Instantiate  │────▶│ Scene Tree   │                         │
│  │ Pool         │     │ (deferred)   │                         │
│  │ (staggered)  │     │              │                         │
│  └─────────────┘     └──────────────┘                         │
│                                                                │
│  Rings:                                                        │
│  - Active ring (radius 1): in tree, processing, visible       │
│  - Buffer ring (radius 2): instantiated, hidden, paused       │
│  - Preload ring (radius 3): loaded as PackedScene, not inst.  │
│  - Beyond: unloaded from memory                               │
└──────────────────────────────────────────────────────────────┘
```

**Critical detail**: The buffer ring (pre-instantiated but hidden) eliminates the instantiation spike when the player moves into a new chunk. The chunk is already a node — just make it visible and enable processing.

### 6.3 Pattern: Background Asset Preparation

For games that need assets ready before they're visible (e.g., cutscene triggers, boss rooms):

```gdscript
# PreloadTrigger.gd — Attach to an Area3D
extends Area3D

@export var scene_to_preload: String
var _preloaded_scene: PackedScene
var _preloaded_instance: Node

func _on_body_entered(_body):
    # Player entered trigger zone — start preloading
    ResourceLoader.load_threaded_request(scene_to_preload)

func _process(_delta):
    if _preloaded_instance:
        set_process(false)
        return
    
    var status := ResourceLoader.load_threaded_get_status(scene_to_preload)
    if status == ResourceLoader.THREAD_LOAD_LOADED:
        _preloaded_scene = ResourceLoader.load_threaded_get(scene_to_preload)
        # Pre-instantiate while player is still approaching
        _preloaded_instance = _preloaded_scene.instantiate()
        # Don't add to tree yet

func activate():
    if _preloaded_instance:
        get_tree().current_scene.add_child.call_deferred(_preloaded_instance)
    else:
        # Fallback: blocking load
        var scene := load(scene_to_preload)
        get_tree().current_scene.add_child(scene.instantiate())
```

### 6.4 Pattern: Progressive Loading (Staggered Instantiation)

When you must instantiate many objects but can't pre-pool them:

```gdscript
# StaggeredLoader.gd
extends Node

var _queue: Array[PackedScene] = []
var _parent: Node
var _per_frame: int = 2  # Max instantiations per frame
signal all_loaded

func start(scenes: Array[PackedScene], parent: Node, per_frame: int = 2):
    _queue = scenes.duplicate()
    _parent = parent
    _per_frame = per_frame
    set_process(true)

func _process(_delta):
    var count := mini(_per_frame, _queue.size())
    for i in count:
        var scene := _queue.pop_front()
        var instance := scene.instantiate()
        _parent.add_child.call_deferred(instance)
    
    if _queue.is_empty():
        set_process(false)
        all_loaded.emit()
```

### 6.5 Pattern: Object Pooling

Pre-instantiate objects during loading, reuse them at runtime:

```gdscript
# Pool.gd
extends Node

var _pool: Array[Node] = []
var _scene: PackedScene

func initialize(scene: PackedScene, count: int):
    _scene = scene
    for i in count:
        var obj := scene.instantiate()
        obj.set_process_mode(Node.PROCESS_MODE_DISABLED)
        obj.hide()
        add_child(obj)
        _pool.append(obj)

func acquire() -> Node:
    if _pool.is_empty():
        # Fallback: create new (will cause micro-hitch)
        var obj := _scene.instantiate()
        add_child(obj)
        printerr("Pool exhausted — dynamic instantiation occurred")
        return obj
    var obj := _pool.pop_back()
    obj.set_process_mode(Node.PROCESS_MODE_INHERIT)
    obj.show()
    return obj

func release(obj: Node):
    obj.set_process_mode(Node.PROCESS_MODE_DISABLED)
    obj.hide()
    _pool.append(obj)
```

---

## 7. Failure Cases

### 7.1 "Async Loading" That Still Causes Frame Drops

**Scenario**: Developer uses `load_threaded_request()` but still sees hitches.

**Root cause**: The loading itself is async, but:
1. `instantiate()` runs on main thread → 40–60ms spike
2. `add_child()` triggers `_ready()` cascades on complex scenes
3. First-frame shader compilation when new materials render

**Reproduction**:
```gdscript
# This WILL hitch despite "async" loading
func _on_load_complete():
    var scene: PackedScene = ResourceLoader.load_threaded_get(path)
    var instance := scene.instantiate()  # ← 40-60ms spike HERE
    add_child(instance)                   # ← _ready() cascade
    # First render frame: shader compilation stutter
```

**Fix**: Pre-instantiate during a loading overlay, warm up shaders, use staggered loading.

### 7.2 Thread Misuse Causing Instability

**Scenario**: Developer tries to instantiate scenes on a background thread.

```gdscript
# DANGEROUS — will crash or corrupt state
func _thread_function():
    var scene := load("res://enemy.tscn")  # OK in some cases
    var enemy := scene.instantiate()        # UNSAFE — scene tree ops
    get_node("/root").add_child(enemy)      # CRASH — not thread-safe
```

**Correct approach**: Only use `ResourceLoader` for threaded loading. All scene tree manipulation must happen on the main thread via `call_deferred()`.

### 7.3 Memory Bloat from Improper Streaming

**Scenario**: Chunks are loaded but never freed. Player explores large world → memory grows unbounded.

```gdscript
# MEMORY LEAK — chunks loaded but never unloaded
var loaded_chunks: Dictionary = {}

func load_chunk(coord: Vector2i):
    var scene := load(chunk_path(coord))
    var instance := scene.instantiate()
    add_child(instance)
    loaded_chunks[coord] = instance
    # No unloading logic → memory grows forever
```

**Fix**: Implement unloading ring. When chunks leave the preload radius, `queue_free()` the instance AND erase the dictionary entry so the `PackedScene` resource can be freed by reference counting.

### 7.4 use_sub_threads Causing Main Thread Stalls

**Scenario**: `use_sub_threads = true` spawns threads per core, competing with the main thread for CPU time.

**Evidence**: [Forum report](https://forum.godotengine.org/t/background-loading-freezing-main-thread/54782) — "Setting sub thread to true will spawn many threads. Probably at least one per core. Which could affect scheduling of the main thread."

**Decision rule**: Use `use_sub_threads = false` for streaming during gameplay. Use `true` only during dedicated loading screens where frame drops are acceptable.

### 7.5 CACHE_MODE_IGNORE Causing Infinite Loops

**Scenario**: Using `CACHE_MODE_IGNORE` with resources that have circular dependencies.

**Evidence**: [Issue #111385](https://github.com/godotengine/godot/issues/111385) — can result in null resources or infinite loops.

**Decision rule**: Default to `CACHE_MODE_REUSE`. Only use `CACHE_MODE_IGNORE` when you explicitly need a fresh copy and understand the dependency graph.

---

## 8. Benchmarks & Measured Behavior

### 8.1 Blocking vs Threaded Load

| Metric | `load()` (blocking) | `load_threaded_request()` | Delta |
|--------|---------------------|---------------------------|-------|
| Total load time | ~2000ms | ~2000ms | Same |
| Frame time during load | 2000ms (frozen) | 16.6ms (normal) | **120x better** |
| Player-perceived freeze | Yes | No | Eliminated |
| Progress feedback | Impossible | 0–100% via polling | Enabled |

**Note**: Threaded loading does not make loading faster — it makes it non-blocking. Total wall-clock time is similar (slightly higher due to thread overhead).

### 8.2 Preload vs Runtime Load

| Metric | `preload()` | `load()` at runtime | `load_threaded_request()` |
|--------|-------------|---------------------|---------------------------|
| When cost is paid | Script parse time | Call site | Background |
| Frame impact at use site | Zero | Full blocking | Zero (if polled) |
| Path flexibility | Literal only | Dynamic | Dynamic |
| Memory | Held for script lifetime | Cached after first load | Cached after completion |
| Best for | Small, frequent assets | Conditional assets | Large scenes, streaming |

### 8.3 Instantiation Cost (Community-Measured)

| Operation | Typical Duration | Source |
|-----------|-----------------|--------|
| `instantiate()` — simple scene (10 nodes) | < 1ms | General consensus |
| `instantiate()` — medium scene (100 nodes) | 5–15ms | Forum reports |
| `instantiate()` — complex scene (200+ nodes, physics) | 40–60ms | [Proposal #1801](https://github.com/godotengine/godot-proposals/issues/1801) |
| `add_child()` — triggers `_ready()` cascade | 1–20ms | Depends on script complexity |
| Shader compilation — first draw of unique material | 16–200ms+ | [Issue #61233](https://github.com/godotengine/godot/issues/61233) |
| Object pool `acquire()` (show + enable) | < 0.1ms | Negligible |

### 8.4 Frame Budget Context

At 60fps, each frame has a **16.6ms budget**. At 120fps, **8.3ms**.

| Operation | 60fps Frames Lost | 120fps Frames Lost |
|-----------|-------------------|---------------------|
| Complex instantiate (60ms) | 3–4 frames | 7–8 frames |
| Shader compile (100ms) | 6 frames | 12 frames |
| Blocking load (2000ms) | 120 frames | 240 frames |
| Pool acquire (0.1ms) | 0 | 0 |

---

## 9. Decision Rules

```
┌──────────────────────────────────────────────────────────────────┐
│                      DECISION FLOWCHART                           │
│                                                                    │
│  Is the resource small and used every frame?                      │
│  ├── YES → preload()                                              │
│  └── NO ↓                                                         │
│                                                                    │
│  Is the resource path known at compile time?                      │
│  ├── YES, and it's medium-sized → preload() or load() at _ready  │
│  └── NO, or it's large ↓                                          │
│                                                                    │
│  Can you show a loading screen?                                   │
│  ├── YES → load_threaded_request() + loading screen               │
│  │         use_sub_threads = true (OK during loading screen)      │
│  │         Warm up shaders behind overlay                         │
│  └── NO (seamless required) ↓                                     │
│                                                                    │
│  Is the content predictable (player moving in direction)?         │
│  ├── YES → Chunk streaming with preload ring                     │
│  │         load_threaded_request() with use_sub_threads = false   │
│  │         Pre-instantiate in buffer ring                         │
│  └── NO (unpredictable spawning) ↓                                │
│                                                                    │
│  Are objects short-lived and frequently created?                  │
│  ├── YES → Object pooling                                         │
│  └── NO → Staggered instantiation (1-2 per frame)                │
└──────────────────────────────────────────────────────────────────┘
```

### Quick Decision Table

| Situation | Loading Method | Instantiation Strategy | Shader Strategy |
|-----------|---------------|----------------------|-----------------|
| Menu → Level | `load_threaded_request` + loading screen | Single `instantiate()` (acceptable hitch behind overlay) | Warm up behind overlay |
| Level → Level (seamless) | `load_threaded_request` + crossfade | Pre-instantiate during crossfade | Warm up during crossfade |
| Open world chunks | `load_threaded_request` per chunk | Buffer ring pre-instantiation | Warm up when chunk enters buffer ring |
| Bullets/particles | `preload()` at script load | Object pool | Warm up at game start |
| Boss room (triggered) | `load_threaded_request` on area trigger | Pre-instantiate on trigger | Warm up on trigger |
| Procedural content | `load()` or generate in thread | Staggered instantiation | Warm up progressively |

---

## 10. Tradeoff Tables

### 10.1 Latency vs Memory

| Approach | Memory Cost | Load Latency | Frame Hitch Risk | Complexity |
|----------|------------|-------------|------------------|------------|
| Preload everything | Very High | Zero | None | Low |
| Load on demand (blocking) | Low | High | Severe | Low |
| Threaded load on demand | Low | Medium (async) | Instantiation only | Medium |
| Chunk streaming (3-ring) | Medium | Low (predicted) | Minimal | High |
| Full object pooling | High (pre-allocated) | Zero | None | Medium |

### 10.2 use_sub_threads Tradeoff

| Setting | Load Speed | Main Thread Impact | Best Context |
|---------|-----------|-------------------|-------------|
| `false` | Slower (single thread) | Minimal | During gameplay, seamless streaming |
| `true` | Faster (multi-thread) | Can cause frame drops | During loading screens only |

### 10.3 Cache Mode Tradeoff

| Mode | Behavior | Use Case | Risk |
|------|----------|----------|------|
| `CACHE_MODE_REUSE` | Returns cached if available | Default for everything | None |
| `CACHE_MODE_IGNORE` | Always loads fresh from disk | Hot-reload, unique instances | Infinite loops with circular deps |
| `CACHE_MODE_REPLACE` | Refreshes cached data | Live content updates | Unexpected state changes |

### 10.4 Streaming Granularity

| Chunk Size | Pros | Cons |
|-----------|------|------|
| Small (16–32 units) | Fine-grained control, low per-chunk cost | Many chunks to manage, overhead from frequent load/unload |
| Medium (64–128 units) | Good balance | Moderate instantiation cost |
| Large (256+ units) | Fewer chunks, simpler management | High instantiation cost, more memory per chunk |

**Rule of thumb**: Target chunk instantiation time under 5ms (≈50 nodes or fewer per chunk at medium complexity).

---

## 11. Sources

1. [Godot Official Docs — ResourceLoader Class](https://docs.godotengine.org/en/stable/classes/class_resourceloader.html) — API reference for all threaded loading methods
2. [Godot Official Docs — Background Loading Tutorial](https://docs.godotengine.org/en/stable/tutorials/io/background_loading.html) — Canonical async loading pattern
3. [Godot Official Docs — Using Multiple Threads](https://docs.godotengine.org/en/stable/tutorials/performance/using_multiple_threads.html) — Thread safety rules and patterns
4. [Godot Proposal #1801 — PackedScene.instance() lag](https://github.com/godotengine/godot-proposals/issues/1801) — Measured 40–60ms instantiation cost, request for interactive instancing
5. [Godot Issue #61233 — Shader compilation stutter](https://github.com/godotengine/godot/issues/61233) — Confirmed first-draw shader compilation regression in Vulkan renderer
6. [Godot Issue #111385 — CACHE_MODE_IGNORE loops](https://github.com/godotengine/godot/issues/111385) — Cache mode edge cases
7. [Godot Issue #74051 — Thread pool exhaustion on web](https://github.com/godotengine/godot/issues/74051) — use_sub_threads risk on web exports
8. [Godot Forum — Background Loading Freezing Main Thread](https://forum.godotengine.org/t/background-loading-freezing-main-thread/54782) — Real-world report of threaded loading still causing freezes
9. [Godot Forum — Lazy Load Scene Stutters](https://forum.godotengine.org/t/how-to-lazy-load-a-scene/134201) — Confirmation that instantiation is the real bottleneck
10. [UhiyamaLab — Dynamic Loading and Resource Management](https://uhiyama-lab.com/en/notes/godot/dynamic-loading-resource-management) — Comprehensive preload/load/threaded comparison
11. [UhiyamaLab — Object Pooling Guide](https://uhiyama-lab.com/en/notes/godot/godot-object-pooling-basics/) — Production pooling patterns for Godot 4
12. [Hortopan Blog — Threaded Loading](https://blog.hortopan.com/how-to-speed-up-loading-times-in-your-godot-game-by-using-resourceloader-load_threaded_request/) — Practical threaded loader implementation
13. [Yogoda/ZoneLoadingSystem](https://github.com/Yogoda/ZoneLoadingSystem) — Reference zone-based streaming architecture (Godot 3, concepts apply to 4)
14. [Godot Proposal #1197 — Level Streaming Tools](https://github.com/godotengine/godot-proposals/issues/1197) — Feature request for built-in streaming support

---

*Content was rephrased for compliance with licensing restrictions. All technical claims are sourced from official documentation, engine issue trackers, and community-measured results.*
