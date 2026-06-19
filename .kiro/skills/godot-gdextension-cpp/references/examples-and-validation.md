# Examples and Validation

Use this file before finalizing GDExtension work.

## Example Input

Task:

- add a `VoxelGenerator` GDExtension service for chunk-density processing
- keep scene creation and world attachment in GDScript
- support a background worker for chunk generation without touching the active scene tree from that worker
- keep the build and reload workflow honest for the current platform

## Good Output Shape

```md
- files changed:
  - native/terrain/terrain.gdextension
  - native/terrain/SConstruct
  - native/terrain/src/register_types.cpp
  - native/terrain/src/voxel_generator.h
  - native/terrain/src/voxel_generator.cpp
  - scripts/world/chunk_streamer.gd
- project surfaces inspected:
  - project.godot
  - native/terrain/terrain.gdextension
  - native/terrain/SConstruct
  - native/terrain/godot-cpp
  - scripts/world/chunk_streamer.gd
  - target Godot version and precision assumptions
- sync or script-check steps:
  - `refresh_filesystem` after changing `native/terrain/terrain.gdextension` or `scripts/world/chunk_streamer.gd`
  - `get_diagnostics` on `scripts/world/chunk_streamer.gd` if the GDScript glue changed
- ABI and build decisions:
  - godot-cpp aligned to the target 4.x minor version
  - API metadata regenerated from the target executable
  - debug and release libraries kept distinct
  - Windows hot reload treated as restart-required after structural changes
- boundary design:
  - `VoxelGenerator` extends `RefCounted`
  - one coarse `generate_chunk_data()` call accepts packed input and returns packed output
  - GDScript remains responsible for mesh instance creation and scene attachment
- ownership and threading:
  - `Ref<VoxelGenerator>` used across the script boundary
  - worker thread computes data only
  - completion handoff returns to the main thread before signals or scene work
- validation performed:
  - ABI alignment, registration shape, ownership signatures, thread handoff, and `.gdextension` mappings were reviewed
  - if GDScript glue changed, `get_diagnostics` is clean after `refresh_filesystem`
- limits:
  - runtime smoke test and rebuild-loop verification still require editor-connected validation
```

## Reference Blueprints

### Minimal Runtime Extension Layout

```text
native/my_extension/
|- my_extension.gdextension
|- SConstruct
|- godot-cpp/
`- src/
   |- register_types.cpp
   |- my_service.h
   `- my_service.cpp
```

Checks:

- `.gdextension` names the correct entry symbol and platform libraries.
- `register_types.cpp` uses `InitObject`, registers initializer or terminator callbacks, and sets the minimum initialization level explicitly.
- Runtime-facing classes usually register at `MODULE_INITIALIZATION_LEVEL_SCENE`.
- GDScript remains the orchestration layer unless there is a documented reason to move more logic down.

Minimal registration skeleton:

```cpp
void initialize_my_extension(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    ClassDB::register_class<MyService>();
}

extern "C" GDExtensionBool GDE_EXPORT my_extension_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization
) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_my_extension);
    init_obj.register_terminator(uninitialize_my_extension);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
```

Minimal binding skeleton:

```cpp
void MyService::_bind_methods() {
    ClassDB::bind_method(D_METHOD("process_chunk", "input"), &MyService::process_chunk);
}
```

### Safe Bulk-Processing Boundary

```text
GDScript
`- packages input data once per job
   `- calls one coarse native method
      `- C++ processes packed data without per-element script callbacks
         `- main thread receives result and applies scene changes
```

Checks:

- The boundary is shaped around jobs or chunks, not nodes or pixels.
- Packed arrays or resource wrappers carry the bulk data.
- Native code returns data, status, or a deferred completion event instead of mutating the live scene directly.

### Editor-Only Extension Boundary

Use editor initialization only when the feature is truly editor-only, such as import tooling or inspector helpers.

Checks:

- Editor-only registration does not silently become a runtime gameplay dependency.
- Exported builds are not expected to load editor-only classes.

## Validation Ladder

- Editor-connected:
  - build the extension in the intended debug or release mode
  - call `refresh_filesystem` after changing `.gdextension` manifests or any GDScript glue files the editor must reload
  - open the Godot project and confirm the extension loads without registration errors
  - instantiate the exposed class from GDScript or the inspector as appropriate
  - run a smoke scene or tool flow and verify the main-thread handoff path
  - close the project and confirm no native leak warnings appear
  - if class bindings changed, restart the editor and revalidate instead of trusting hot reload
- Filesystem-only:
  - verify `.gdextension`, `SConstruct`, `godot-cpp`, and source registration files are aligned
  - verify `GDCLASS`, `_bind_methods()`, initialization level, and entry symbol shape
  - verify ownership signatures, packed-data boundary design, and thread handoff points
  - end with the exact runtime validation step still required
- Blocked:
  - stop if the engine version, precision, native layout, or calling layer cannot be identified

## Checklist

- The exact engine target and precision were inspected first.
- `.gdextension` and `SConstruct` stay synchronized with the source tree.
- Editor-connected validation uses `refresh_filesystem` before trusting changed `.gdextension` or GDScript glue state.
- Registration uses explicit initialization level and `ClassDB` bindings.
- `Ref<T>` is used for boundary-safe `RefCounted` types.
- Worker threads return data to the main thread instead of mutating the live scene.
- Reload claims are conservative when Windows locking or structural API edits are involved.
- Validation claims match the real operating mode.

## Common Failures

- The extension is compiled against the wrong Godot minor version or wrong generated API metadata.
- Single-precision and double-precision builds are mixed.
- A native service is exposed as many tiny per-frame calls instead of one coarse job interface.
- A raw `RefCounted*` escapes into GDScript.
- A worker thread attaches nodes, frees nodes, or emits signals directly into gameplay callbacks.
- Structural binding edits rely on hot reload and leave stale editor instances behind.
- Changed `.gdextension` or GDScript glue files were never refreshed in the editor before validation claims were made.
- The final note claims runtime validation that never happened.

## Escalate

- Runtime choice is still debatable or the task may belong in GDScript or C# -> `godot-architect`
- The request has become broad feature implementation rather than native-boundary work -> `godot-scope`
- Scene or resource ownership is risky -> `godot-scene-resource`
- The skill workflow itself needs correction -> `godot-retro`
