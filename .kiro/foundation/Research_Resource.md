# Resource Ownership & Mutation Semantics in Godot 4.x

## Core memory and identity model

Godot’s semantics around *ownership* are easier to reason about if you separate three concerns: lifetime (who frees what), identity (what “the same” means), and mutation (who can change shared data).

At the base, most engine types derive from `Object`. Most `Object` instances do **not** manage their own memory, so you generally must free them (directly or indirectly) to avoid leaks. Two notable exceptions are `Node` (freeing a node frees its children) and `RefCounted` (and thus `Resource`), which self-delete when no longer referenced. citeturn22search4turn21view0

`RefCounted`’s lifetime is governed by an internal reference counter; you typically do not call `Object.free()` on it. However, reference counting does **not** collect cycles: two `RefCounted` objects that reference each other can remain alive forever. Godot explicitly calls this out and recommends breaking cycles with weak references via `@GlobalScope.weakref()`. citeturn21view0

`Resource` sits on top of this: it is a `RefCounted` data container designed for serialisation (text/binary resource files, scene files, and subresources). Godot’s official class reference stresses three foundational behaviours that drive nearly all “hidden state” bugs:

* Resources are reference-counted and freed when no longer used. citeturn27view0  
* Resources are **cached globally by path**. Once a resource is loaded and cached, loading again using the same path returns the cached instance; it is removed from cache only when all references are released. citeturn27view0turn7view0  
* A `PackedScene` is also a `Resource`, capable of instantiating its node hierarchy many times. citeturn27view0turn28view0  

This implies a strong architectural default: by design, *resources are intended to be shareable data blobs*, and the engine optimises for reuse and caching. citeturn28view0turn27view0

Two identity details matter for save/load and debugging:

* `resource_path` is the resource’s unique path. If a resource is embedded inside a scene, the `resource_path` becomes the `PackedScene`’s path followed by a unique identifier. citeturn26view0  
* `resource_scene_unique_id` exists as a scene-relative “unique identifier”; if empty it is generated when saved inside a `PackedScene`, and if collisions occur only the earliest resource in the scene hierarchy keeps the ID. This is editor-centric and can be regenerated. citeturn26view0  

## How sharing actually happens in practice

Godot’s official scripting tutorial summarises the default: when the engine loads a resource from disk it loads it once; if it is already in memory, loading returns the same copy. citeturn28view0turn27view0 That statement is *only safe* if you treat those resources as effectively immutable (configuration/assets). The moment you mutate a shared resource at runtime, you are mutating global shared state.

There are three common sharing vectors you must model explicitly:

### Sharing via the global resource cache
Any `load("res://…")` / `preload("res://…")` / `ResourceLoader.load()` call that uses the default cache mode can give you a previously loaded instance rather than a fresh instance. Godot documents the cache and exposes `ResourceLoader.CacheMode` controls, including `CACHE_MODE_IGNORE` (do not read/store the main resource and its subresources in cache, but dependencies still load with `CACHE_MODE_REUSE`; use `CACHE_MODE_IGNORE_DEEP` to propagate ignore to dependencies) and deeper variants. citeturn27view0turn7view0

### Sharing via scene instancing
Even if a resource is “built-in” (embedded in the `.tscn`), instancing a scene multiple times typically still results in the engine loading **one copy** of that built-in resource by default. Godot’s tutorial explicitly warns: “Even if you save a built-in resource, when you instance a scene multiple times, the engine will only load one copy of it.” citeturn28view0

The practical effect: two scene instances can share the *same* `StandardMaterial3D`, `PhysicsMaterial`, `SpriteFrames`, custom `Resource`, etc., unless you make it unique per instance (via editor tooling or via `resource_local_to_scene` / duplication). citeturn28view1turn26view0

### Sharing via “copying” nodes or data structures
Duplicating a `Node` (Ctrl+D in the editor, or `duplicate()` in script) copies property values, but any `Resource`-typed properties inside those properties remain **references** unless you explicitly duplicate them. Godot’s instancing tutorial uses `PhysicsMaterial` as the canonical example: changing it affects all instances because it’s a resource; you have to “Make Unique” to edit one instance independently. citeturn28view1

A useful mental model is to think in terms of a graph, not a tree:

```text
                (global cache)
         "res://weapon.tres" ───▶ [WeaponConfig Resource]
                                      ▲          ▲
                                      │          │
[Enemy#1 Node] ──weapon_config────────┘          └────────weapon_config── [Enemy#2 Node]

Mutation of WeaponConfig affects both enemies unless a per-entity copy exists.
```

So, define these terms in your architecture:

**Shared resource**: multiple owners hold the same `Resource` reference (intended for immutable configs/assets). This is the default due to caching and reference semantics. citeturn27view0turn28view0

**Unique resource**: each entity/instance holds its own `Resource` object (created with duplication, “Make Unique”, or explicit construction) so mutation is isolated. Godot’s docs explicitly position “Make Unique” as the way to isolate per-instance edits. citeturn28view1

**Local-to-scene resource**: a resource marked `resource_local_to_scene = true` which Godot duplicates for each instantiated scene instance (with important caveats covered below). citeturn26view0turn16view0

## `resource_local_to_scene` semantics and edge cases

### What it guarantees when it applies
In Godot 4.6’s class reference, `resource_local_to_scene` means: if enabled, the resource is duplicated for each instance of all scenes using it, allowing runtime modification in one scene instance without affecting others. citeturn26view0

Internally (engine source), this happens during scene instantiation. The `SceneState::make_local_resource()` path checks `res->is_local_to_scene()` and builds a per-scene-instance mapping: for the *main* scene it configures the resource for the local scene; for *instanced* scenes it creates a copy via `duplicate_for_local_scene()` and stores it in a cache keyed by the scene’s “base” node. citeturn16view0turn15view1turn17view2

Two subtle but crucial properties fall out of this implementation:

* **Local-to-scene is per *scene instance*, not per node**. The mapping is keyed by a “base” node that represents the scene instance scope; once a local resource is duplicated for that base, subsequent uses reuse that same duplicated resource within the instance. citeturn16view0turn15view3  
* After instantiation, Godot calls `setup_local_to_scene()` on each remapped resource, which then invokes `_setup_local_to_scene()` (and emits a deprecated signal). This is where you can initialise per-instance randomisation or derived values. citeturn15view2turn26view0turn14view2  

From the public API side, Godot also exposes `get_local_scene()`: it returns the `local_scene` member pointer, which is only populated during the `duplicate_for_local_scene()` / `configure_for_local_scene()` code paths that run for local-to-scene resources during `PackedScene` instantiation. The method does not check the `local_to_scene` flag directly — it simply returns the pointer (or `null` if the resource was never configured for a local scene). citeturn26view0turn27view0

### When it “silently fails” (or looks like it failed)
These are the failure modes that most often produce hidden state bugs:

* **Setting the flag too late**: changing `resource_local_to_scene` at runtime has no effect on already created duplicates. This is explicitly documented, and often feels “silent” because nothing errors—your instances just keep sharing. citeturn26view0  
* **Using a workflow other than `PackedScene.instantiate()`**: the behaviour is defined in terms of scene instancing; other duplication paths (node duplication in-editor, custom cloning code) won’t necessarily trigger the same remapping pipeline. The engine’s instancing tutorial even mixes “duplicate node” (Ctrl+D) with the warning that resources must be made unique to edit independently—because duplication alone doesn’t imply resource uniqueness. citeturn28view1  
* **Resources inside arrays/dictionaries were historically a problem area**: in 4.4, `Resource.duplicate(subresources=true)` explicitly stated: “Subresources inside Array and Dictionary properties are never duplicated.” citeturn4view1 **This limitation was resolved in 4.5+/4.6**: the current `_duplicate_recursive()` does recurse into Arrays and Dictionaries. However, `resource_local_to_scene` handling in arrays/dicts has had engine bugs/regressions in some versions (see Failure Analysis) — verify behavior on your specific engine version. citeturn23view0turn23view3  
* **Not actually instanced from a `PackedScene`**: `get_local_scene()` returning `null` is a strong signal that you are not in the local-to-scene contract described above (either not instanced from a packed scene, or the local remapping could not occur). citeturn26view0turn23view0  

## `duplicate()` and deep duplication strategies

### The contract in 4.6
Godot 4.6 defines `Resource.duplicate(deep=false)` as copying exported / `PROPERTY_USAGE_STORAGE` properties. With `deep=false`, nested `Array`, `Dictionary`, and `Resource` properties are not duplicated and are shared; with `deep=true`, nested arrays/dictionaries/packed arrays are duplicated recursively, but *resources found inside are only duplicated if they are “local”* (equivalent to `DEEP_DUPLICATE_INTERNAL`). citeturn26view0turn27view0

Godot also adds `duplicate_deep(deep_subresources_mode)` with `DeepDuplicateMode` values:

* `DEEP_DUPLICATE_NONE`: duplicate arrays/dicts but do not duplicate subresources at all (still point to original resources). citeturn26view0  
* `DEEP_DUPLICATE_INTERNAL`: duplicate only subresources without a path or with a scene-local path. citeturn3view0turn26view0  
* `DEEP_DUPLICATE_ALL`: duplicate every subresource, even ones with non-local paths (explicitly warned as potentially duplicating big externally stored resources). citeturn3view0turn26view0  

Two property-flag escape hatches exist: `PROPERTY_USAGE_ALWAYS_DUPLICATE` and `PROPERTY_USAGE_NEVER_DUPLICATE` on subresource properties. citeturn26view0turn3view1

At the engine level, duplication is implemented with a remap cache to ensure each resource in the graph is duplicated only once per duplication session and then reused where referenced multiple times. The 4.6 docs describe this, and the engine source shows a thread-local remap cache used by `duplicate()` / `duplicate_deep()`. citeturn26view0turn13view3turn13view0

### Version pitfall: 4.4 vs 4.5+ semantic shift
If you are maintaining a project across the 4.x line, be aware of a meaningful behavioural change:

*In Godot 4.4*, `duplicate(subresources=true)` promised deep copy of nested **subresources**, but explicitly excluded subresources inside arrays/dictionaries (never duplicated). **This was fixed in later 4.x versions.** citeturn4view1turn4view2  
*In Godot 4.6*, `duplicate(deep=true)` does deep duplication of arrays/dictionaries too, and adds explicit deep-subresource modes. citeturn26view0  

This matters because many architectures store child resources in exported arrays (inventories, upgrade lists, stat modifiers, etc.). Code that looked “correct” under a mental model of “duplicate(true) deep-copies everything” was never correct in 4.4, and even in later versions you still need to understand the `DeepDuplicateMode` you actually want. citeturn4view1turn26view0turn3view0

### Performance cost of deep duplication
Godot’s own `DeepDuplicateMode` docs effectively define your cost envelope:

* Deep duplication duplicates nested arrays and dictionaries recursively (allocation + traversal proportional to container size). citeturn26view0turn3view1  
* `DEEP_DUPLICATE_ALL` duplicates even externally stored subresources (“potentially big resources stored separately”). This can create large memory spikes if used on trees that reference textures/meshes/materials/etc. citeturn3view0turn26view0  
* Remap caching prevents duplicating the same subresource multiple times within one duplication call, which helps when graphs have shared nodes. citeturn26view0turn13view3  

Finally, there is a sharp “gotcha”: for custom resources, `duplicate()` can fail if your `Object._init()` requires parameters. This is documented in 4.6 and leads to partial/failed duplication patterns if you expect polymorphic construction. citeturn26view0

## Failure analysis with reproducible scenarios

The scenarios below are chosen because they are (a) common in production, (b) hard to spot in large projects, and (c) grounded in documented behaviour or concrete engine issues.

### Shared mutable state across scene instances via built-in resources

**Setup**  
1) Create `Ball.tscn` with a `PhysicsMaterial` on a physics body.  
2) Instance `Ball.tscn` multiple times into `Main.tscn`.  
3) Modify the `PhysicsMaterial` properties of one instance (in editor or at runtime).

**Expected**  
Changing one ball’s material should only affect that ball.

**Actual**  
All balls’ physics behaviour changes together unless you make the material unique.

**Root cause**  
Godot treats `PhysicsMaterial` as a `Resource`, and resources are shared between instances by default; the official instancing tutorial explicitly calls out that you need “Make Unique” to edit a resource independently per instance. citeturn28view1turn28view0

**Mitigation**  
Treat `.tres`/built-in resources as immutable configs. For per-instance edits, either:
* Use the editor command “Make Unique” for that instance, or
* Store per-instance state on the node (numbers, flags) rather than mutating the shared `Resource`. citeturn28view1turn28view0

### `resource_local_to_scene` appears ignored for arrays/dictionaries

> **Version note**: The `duplicate()` limitation for arrays/dictionaries was fixed in 4.5+/4.6. The `resource_local_to_scene` bug described below may still apply in some versions — verify against your engine version before applying the mitigation.

**Setup**  
Export an array/dictionary containing local-to-scene resources, instance the scene multiple times, and mutate the resources.

**Expected**  
Local-to-scene implies per-scene-instance uniqueness; changes should not leak.

**Actual**  
Instances are shared; `get_local_scene()` returns `null`, and modifying one instance affects the other.

**Root cause**  
This is a confirmed Godot core bug report: local-to-scene “no longer works” when the resource is part of an array or dictionary; step-by-step reproduction describes exactly this, including `get_local_scene() == null` and shared modifications. citeturn23view0turn26view0

**Mitigation**  
If you must store resources in arrays/dicts, add an explicit “instantiate state” step after `PackedScene.instantiate()` that deep-duplicates the relevant data. In 4.6, prefer `duplicate_deep()` with an intentional mode (`INTERNAL` vs `ALL`) rather than assuming `duplicate(true)` semantics. citeturn26view0turn3view0turn23view0

### Local-to-scene fails across nested scenes

**Setup**  
1) Parent `PackedScene` contains a child `PackedScene`.  
2) The child scene exposes a resource (e.g., a `Shape` on `CollisionShape2D`).  
3) Mark that shape resource “Local to Scene”.  
4) Instantiate the parent twice and print the child’s shape resource reference.

**Expected**  
Each parent instance should have its own shape resource.

**Actual**  
Both instantiated parents share the same shape resource.

**Root cause**  
A Godot 4.x issue report describes exactly this nested-scene structure and concludes: “the resource will not be duplicated per scene” when instantiating the parent. citeturn23view1turn26view0

**Mitigation**  
When nesting scenes, do not assume “local-to-scene” automatically propagates through all packing/inheritance patterns. Add a post-instantiate initialisation step in the parent that explicitly duplicates/instantiates the required child resources (or refactor the child so the resource is owned/created by the parent instance at runtime). citeturn23view1turn26view0

### Next-pass materials ignore local-to-scene in some cases

**Setup** (from the reported reproduction)  
1) Create scene `Pillar` with a `MeshInstance3D`.  
2) Assign a `VisualShader` material to “Surface Material Override” and set it Local To Scene.  
3) Create a `StandardMaterial3D` in the “Next Pass” slot, set it Local To Scene.  
4) Instance `Pillar` twice; drive the next-pass material property from a per-instance exported variable.

**Expected**  
Each pillar instance should render with independent next-pass parameters.

**Actual**  
Changing next-pass material parameters on one instance affects the other instances.

**Root cause**  
A Godot issue report documents that Local To Scene works for the root material but is “ignored” for the second material in the Next Pass slot, causing replicated attributes across instances. citeturn23view2turn26view0

**Mitigation**  
Avoid mutating nested material graphs unless you have verified that the engine version correctly remaps the entire chain. If your effect requires per-instance parameters, prefer setting per-instance shader parameters on the node/material instance you control (or duplicate the material chain explicitly in code). citeturn23view2turn26view0

### Regression example: ArrayMesh surfaces + inherited scenes (Godot 4.6)

**Setup** (condensed from the report)  
1) Parent scene uses an `ArrayMesh` with `resource_local_to_scene = true`.  
2) The `ArrayMesh` contains a `StandardMaterial3D` in its `_surfaces` array, also Local To Scene.  
3) Child scene inherits from parent via `instance=ExtResource(...)`, overrides mesh/material similarly.  
4) Instantiate multiple children dynamically (`PackedScene.instantiate()`), then modify one instance’s surface material.

**Expected**  
Each instantiated child has its own independent material copy.

**Actual**  
All dynamically instantiated instances share the same material; modifying one changes all.

**Root cause**  
A Godot 4.6 stable issue report identifies these exact conditions (scene inheritance + ArrayMesh `_surfaces` + both marked local-to-scene) and states it reproduces in 4.6 but not 4.5. citeturn23view3turn16view0

**Mitigation**  
Treat `resource_local_to_scene` as “best effort” across complex nested/inherited resource graphs and verify behaviour per minor version. The report provides a workaround: disable local-to-scene on the parent scene’s resources and only enable it on the directly instantiated child. citeturn23view3turn26view0

### `duplicate(true)`/deep duplication does not duplicate resources inside arrays (historical & practical)

**Setup**  
A custom resource contains:
```gdscript
extends Resource
class_name CustomResource
@export var array_of_subresources: Array[Resource]
@export var subresource: Resource
```
Populate `array_of_subresources` with a resource and run:
```gdscript
var cr_dup := cr.duplicate(true)
assert(cr.subresource != cr_dup.subresource) # expected success
assert(cr.array_of_subresources[0] != cr_dup.array_of_subresources[0]) # expected, but fails in affected versions
```

**Expected**  
Deep duplication duplicates both the direct subresource and array elements.

**Actual**  
Array elements are still shared even though direct subresource duplicates.

**Root cause**  
This is documented both as (a) explicit 4.4 behaviour (“Subresources inside Array and Dictionary properties are never duplicated.”) and (b) concrete bug reports showing `duplicate(true)` failing for array-contained subresources. citeturn4view1turn24view1

**Mitigation**  
In 4.6, use `duplicate(deep=true)` / `duplicate_deep()` intentionally and build regression tests around your specific resource graphs (especially arrays/dictionaries and nested scenes). When supporting older 4.x, implement an explicit “deep clone config” step that traverses arrays/dictionaries and duplicates resources on your own rules. citeturn26view0turn24view1turn4view1

### Cache-driven save/load bug: “reload” returns mutated in-memory instance

**Setup**  
1) Load a savegame resource from `user://save.tres` using the default cache mode.  
2) Modify it in memory.  
3) Call `ResourceLoader.load("user://save.tres")` again expecting pristine on-disk data.

**Expected**  
Second load returns fresh data from disk.

**Actual**  
Second load returns the same cached resource instance (already mutated).

**Root cause**  
Godot documents a global resource cache keyed by path: subsequent loads using the path return the cached reference. `ResourceLoader` also documents cache modes, including `CACHE_MODE_IGNORE` to bypass caching. citeturn27view0turn7view0

**Mitigation**  
When loading savegames you intend to treat as “snapshots”, use `ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)` or `CACHE_MODE_IGNORE_DEEP` for nested dependencies, depending on your save format. citeturn7view0turn27view0

## Production-safe guidelines for scalable architecture

This section is intended to be used as a standing engineering guideline to prevent “hidden state” as your project grows.

### Rules of thumb (DO / DON’T)

**DO treat resources as immutable by default**  
Godot itself frames resources as “data containers” and emphasises the “load once / reuse” model. This is a strong hint: resources are optimised for sharing, so your safest baseline is to treat them as *read-only configuration* once gameplay starts. citeturn28view0turn27view0

**DO isolate per-entity runtime state outside shared resources**  
Per-entity state belongs on nodes or on per-entity `RefCounted` state objects, not on shared `.tres` configs. This avoids both cache-sharing and instancing-sharing pitfalls. citeturn28view0turn21view0

**DO use `resource_local_to_scene` only when you want per-*scene-instance* copies**  
Local-to-scene is implemented with per-instance remapping keyed to a base node; it’s not “unique per node”, and it can be fragile across certain nested/inherited resource graphs. citeturn16view0turn23view3turn26view0

**DO choose a deliberate duplication mode rather than “just deep copy everything”**  
In 4.6, `duplicate(deep=true)` duplicates arrays/dicts but only duplicates “internal” resources by default; `duplicate_deep()` lets you opt into duplicating all subresources (including external ones) with `DEEP_DUPLICATE_ALL`, which is explicitly described as potentially duplicating large resources. citeturn26view0turn3view0

**DON’T mutate a resource loaded from `res://` unless you mean to mutate global shared state**  
The engine cache is global and keyed by path; mutating a cached resource affects every consumer holding that reference. citeturn27view0turn7view0

**DON’T store cyclic graphs of `RefCounted` without weak references**  
Reference cycles leak under refcounting; Godot explicitly warns about this and recommends `weakref()` to break cycles. citeturn21view0

### Safe patterns for data-driven design using resources

A production-safe pattern is to separate “definition” from “instance state”:

**Definition resources (immutable)**  
Use `.tres` resources as *archetypes*: item definitions, enemy stats, level descriptors, tuning tables. Load them once and never mutate them at runtime. This aligns with Godot’s “loaded once, reuse” model. citeturn28view0turn27view0

**Instance state (mutable)**  
When spawning an entity, create an instance-state object that copies only the fields that must change (HP, cooldown timers, upgrade levels). These can be plain fields on the node, or a dedicated `RefCounted`/`Resource` created at runtime (not loaded from disk, not cached by path unless you explicitly take over a path). citeturn21view0turn27view0

**When you truly need per-instance resource mutation**  
For example, per-instance material tweaks, collision shapes that scale per enemy, or procedural curves. Use one of:

* **Editor-side “Make Unique”** for specific overrides (fastest workflow for level authoring). This is the documented solution to per-instance resource editing in the instancing tutorial. citeturn28view1  
* **`resource_local_to_scene` + `_setup_local_to_scene()`** when the resource is part of a scene instance and you want it duplicated automatically per instantiation. Godot documents `_setup_local_to_scene()` as the supported hook, and engine source shows it is called after remapping. citeturn26view0turn15view2turn14view2  
* **Explicit duplication on spawn** (`duplicate()` / `duplicate_deep()`) when you need strict control (and especially when arrays/dictionaries or nested scenes are involved). Prefer this for systems that must be robust across multiple Godot minor versions. citeturn26view0turn23view3turn24view1  

### When *not* to use resources

Avoid using `Resource` as your primary container for highly volatile runtime state (e.g., values that change every frame) if:

* multiple entities could accidentally share the reference (common in arrays/dicts and nested scenes), citeturn23view0turn23view1  
* you require strict ownership boundaries and deterministic destruction (resources are refcounted and may persist in cache until all references are released), citeturn27view0turn7view0  
* your object graph is prone to cycles (refcount leaks). citeturn21view0  

In those cases, store runtime state on nodes or on dedicated `RefCounted` state objects with clear parent ownership and weak references where necessary. citeturn21view0turn22search4

### Save/load considerations: serialisation, identity, and reference pitfalls

**Serialisation behaviour and “identity vs instance”**  
Godot resources are designed to serialise to disk (`.tres`, `.res`, `.tscn`, `.scn`) and can contain nested subresources. citeturn28view0turn27view0 However, the moment you save references, you must decide whether you are saving:

* **Identity** (“this instance refers to the SwordDefinition at res://defs/sword.tres”), or  
* **Stateful instance** (“this specific sword instance has durability 12/40 and custom rolls”).  

If you save a direct resource reference with a stable `resource_path`, on load it may resolve back to a cached global resource, reintroducing shared-state surprises. citeturn27view0turn7view0

**Use UIDs for robust project references (editor-side)**  
Godot 4.x introduces `ResourceUID`: resource UIDs preserve references even if files are renamed/moved, accessible via `uid://`. citeturn10view0 This is excellent for *project asset identity*.

But note: `ResourceSaver.save()` documents that when the project is running, generated UIDs associated with resources will not be saved because required code runs only in editor mode. citeturn9view0 So for runtime-generated save files, you should not rely on automatic UID generation being persisted.

**Control caching when loading save snapshots**  
Because the engine caches resources by path, you should use `ResourceLoader` cache modes deliberately for save/load. `CACHE_MODE_IGNORE` is explicitly provided to bypass cache for the main resource and its subresources; deeper ignore/replace modes exist for dependency trees. citeturn7view0turn27view0

**Practical production pattern for save games**
Store:
* asset references as `uid://…` (or `res://…` paths) for immutable definitions, citeturn10view0turn28view0  
* instance state as primitive fields (numbers, strings) in a dedicated save resource,  
and load with `CACHE_MODE_IGNORE` if you need a fresh snapshot each time. citeturn7view0turn27view0

---

### Final mindset: the “no hidden shared mutation” contract

If you want a production-safe architecture, adopt a hard rule:

> **No gameplay system is allowed to mutate a resource unless it can prove that resource is unique to the current entity/scene instance.**

Godot gives you tools to prove uniqueness (`resource_local_to_scene`, controlled duplication modes, “Make Unique”, `get_local_scene()` checks), but it also has known edge cases (arrays/dictionaries, nested/inherited scene graphs, specialised engine resource layouts) that require explicit testing per Godot minor version. citeturn26view0turn23view3turn23view0turn24view1