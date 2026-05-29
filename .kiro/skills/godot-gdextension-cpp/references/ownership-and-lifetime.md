# Ownership and Lifetime

Use this file before implementing native classes or debugging leaks and crashes.

## Ownership Matrix

| Kind | Preferred Construction | Ownership Rule | Common Footgun |
| :---- | :---- | :---- | :---- |
| `Object` or `Node` descendants | `memnew(MyNode)` | If added to the active tree, the engine owns destruction. If kept orphaned, native code must `memdelete()` it. | Mixing `add_child()` ownership with manual `memdelete()`, or using plain `new` for Godot objects. |
| `RefCounted` or `Resource` descendants | `Ref<MyType> value; value.instantiate();` | Lifetime is controlled by Godot reference counting through `Ref<T>`. | Returning or storing raw pointers across the GDScript boundary. |
| `Variant` | Stack allocation | Keep it local and pass by value or `const Variant&` as appropriate. | Heap-allocating `Variant*` and losing track of its lifecycle. |
| `Array` or `Dictionary` | Godot container value types | Reads can be shared carefully; structural writes need synchronization when multiple threads are involved. | Resizing or mutating shared containers across threads without a lock. |

## Node and Object Rules

- Use `memnew()` for Godot object allocation so leak tracking stays visible to the engine.
- Once a node is attached with `add_child()`, do not `memdelete()` it manually.
- If a node stays detached, native code owns it and must destroy it explicitly.
- Be conservative with cached raw pointers to scene nodes:
  - `queue_free()` from GDScript or scene teardown can invalidate them immediately
  - reacquire when practical, or store an `ObjectID` and validate before use

## RefCounted Rules

- Prefer `RefCounted` for service-style processors, reusable data wrappers, and resources that cross the language boundary.
- Cross-boundary signatures involving reference-counted Godot types should use `Ref<T>` directly.
- If a third-party C API forces raw-pointer interop, make the temporary lifetime strategy explicit before unwrapping.
- Do not assume GDScript will rescue an unsafe raw pointer contract.

## Variant and Marshaling Rules

- Stack-allocate `Variant` values.
- Use `const Variant&` for flexible inputs when the API truly needs `Variant`.
- Prefer concrete Godot types or packed arrays when the shape is known.
- Treat `PackedByteArray`, `PackedFloat32Array`, and related packed containers as the default bulk-transfer surface for numeric work.

## Thread-Safety Addendum

- Worker threads must not mutate the active `SceneTree`.
- If a worker needs to trigger scene or UI work, defer that handoff back to the main thread.
- If a signal can wake GDScript that touches nodes or UI, emit it on the main thread or via deferred handoff.
- Protect structural `Array` or `Dictionary` writes with a `Mutex` or another explicit synchronization strategy.

## Leak and Crash Triage Questions

- Is this a `Node` that was both attached to the tree and manually deleted?
- Is this a `RefCounted` type crossing the script boundary as a raw pointer?
- Is a cached node pointer being reused after `queue_free()` or scene replacement?
- Is a worker thread touching live scene nodes or emitting signals directly into script callbacks?
- Is a `Variant*` or other heap-allocated wrapper being used where a stack value should exist?
