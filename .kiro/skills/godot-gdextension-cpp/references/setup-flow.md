# Setup Flow

Use this file before touching `.gdextension`, `godot-cpp`, or `SConstruct`.

## Inspect First

- Exact Godot executable and version the project is targeting.
- Whether the engine build is single-precision or double-precision.
- Existing `.gdextension` files, native source layout, and output library names.
- Current `godot-cpp` location, submodule status, and any generated API metadata already checked in.
- Target platforms and any external native libraries that must link or ship with the extension.
- Whether the task changes runtime code, editor-only tooling, or both.

## Stable Setup Sequence

1. Lock the target engine first.
   - Record the exact Godot 4.x minor version the project will build against.
   - If multiple engine binaries are in play, identify which one is authoritative for API generation.
2. Align `godot-cpp` to the target engine.
   - Prefer a pinned checkout or submodule rather than a floating copy.
   - Compile against the minimum minor version you need to support.
   - Do not compile against a newer minor version and expect older engines to load it.
3. Regenerate or verify the extension API metadata with the target executable.
   - A common flow is running the target Godot binary with `--dump-extension-api`.
   - Treat mismatched generated metadata as an ABI blocker, not a warning.
4. Match floating-point precision explicitly.
   - A single-precision extension does not safely load into a double-precision engine build.
   - Rebuild the extension whenever the precision target changes.
5. Verify build outputs and file ownership.
   - Use `target=template_debug` for debug iteration and `target=template_release` for optimized runtime output unless the project already has stricter conventions.
   - Keep debug and release library outputs distinct.
   - Confirm `.gdextension` points at the expected platform-specific libraries.
   - Keep entry symbols and library names synchronized with the source tree.
6. Add third-party native libraries deliberately.
   - Wire include paths, static or shared libraries, and platform-specific artifacts in `SConstruct`.
   - Treat deployment packaging as part of the task, not as an afterthought.
7. Only then add or change native classes and bindings.

## Required Runtime Footprint

Expect these pieces to stay synchronized:

- `.gdextension` file:
  - library paths per platform and build
  - entry symbol
  - `reloadable = true` only when the workflow can support it safely
- `SConstruct`:
  - `godot-cpp` build configuration
  - extension source files
  - target, platform, architecture, and precision flags
  - external include and link settings when needed
- registration source:
  - entry point
  - initializer and terminator
  - minimum initialization level
- native classes:
  - `GDCLASS`
  - `_bind_methods()`
  - Godot-safe ownership and boundary signatures

## ABI Rules

| Concern | Rule |
| :---- | :---- |
| Engine minor version | Forward compatibility can work from older build target to newer minor runtime, not the reverse. |
| Generated API metadata | Must come from the executable the extension is targeting. |
| Floating-point precision | Must match exactly between engine and extension. |
| Changing engine or bindings | Rebuild the extension and re-check `.gdextension` outputs. |
| Structural binding changes | Restart the editor rather than trusting hot reload. |

## Reload and Packaging Rules

- Use `reloadable = true` only for controlled iteration on internal algorithms.
- If class names, `_bind_methods()`, exported properties, or initialization levels change, restart the editor.
- On Windows, expect `.dll` and `.pdb` locking to break in-editor rebuilds unless the project already has a proven workaround.
- If the project does not already implement copied-temp-library or renamed-symbol workflows on Windows, prefer the conservative loop:
  - close the editor
  - rebuild
  - reopen
- Make exported library packaging explicit for every target platform the task claims to support.

## Stop and Escalate

- Engine version or precision is unknown.
- `godot-cpp` provenance is unclear.
- The task quietly requires a custom engine module instead of a GDExtension.
- Platform packaging expectations are claimed but not actually grounded in the project.
