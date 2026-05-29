---
name: godot-gdextension-cpp
description: |
  Create or update Godot 4 GDExtension C++ code with safe godot-cpp setup, ABI alignment, ClassDB registration, ownership discipline, and honest validation.
  Use when a real project has a justified native-code need such as isolated heavy CPU work or third-party C/C++ integration, not ordinary gameplay scripting.
  Also use when someone asks about godot-cpp, .gdextension files, SCons build setup, ClassDB registration, GDCLASS macro, native bindings, Ref<T> ownership, calling C++ from GDScript, or wrapping an existing C/C++ library for Godot.
  If the task involves writing, debugging, or setting up any GDExtension native code, this skill should fire.
---

# Godot GDExtension C++

Build narrowly scoped Godot native code only when the project actually needs native code and the boundary back to GDScript can stay safe.

Read `references/setup-flow.md` first. Read `references/ownership-and-lifetime.md` before implementing or reviewing native classes. Read `references/boundary-design-checklist.md` when designing the API surface or worker-thread behavior. Read `references/examples-and-validation.md` before finalizing the result.

## Responsibility

- Inspect the real project surfaces that native code touches before editing:
  - exact Godot version, executable, and floating-point precision assumptions
  - existing `.gdextension` files, `SConstruct`, `godot-cpp` checkout, source layout, and output library paths
  - the target native classes plus the GDScript, scene, or resource call sites that will use them
  - platform constraints such as Windows reload friction or external native dependencies
- Create or update bounded GDExtension code and build wiring:
  - `InitObject` entry and initialization level
  - `GDCLASS`, `_bind_methods()`, and `ClassDB` exposure
  - `.gdextension` library mapping and SCons build flow
  - safe ownership, threading, and cross-language boundary rules
- Use native file editing for C++ sources, build files, `.gdextension` manifests, and script call sites, then `refresh_filesystem` for any editor-consumed files that must be reloaded before validation.
- Keep the native boundary narrow:
  - isolated heavy computation
  - third-party C or C++ library integration
  - data-oriented processing that can stay mostly inside C++
- Validate with the strongest honest method available and report limits without bluffing.

## Use When

- The project already has a justified reason to leave GDScript for native code.
- The task involves `godot-cpp`, `.gdextension`, SCons, `ClassDB`, `Ref<T>`, or native registration code.
- The hot path can be expressed as bulk processing or library integration instead of per-node chat across the language boundary.
- The task is bounded enough to implement safely inside the current project.

## Do Not Use When

- Scope is still broad and the runtime choice is not approved yet.
- The request is normal gameplay scripting, UI flow, save/load logic, or other high-level orchestration that should stay in GDScript.
- The work is mainly IDE setup, general C++ teaching, or a full engine-module/custom-engine build.
- The workload is better framed as a GPU or compute-shader problem but that tradeoff has not been decided.
- The required engine version, toolchain, or target integration points cannot be inspected yet.

## Workflow

1. Read `references/setup-flow.md`, then inspect the real targets:
   - Godot version, precision, target platforms, and current executable assumptions
   - existing `.gdextension`, `SConstruct`, `godot-cpp`, native source files, and output libraries
   - the GDScript, scene, or resource layer that will call into the extension
   - whether the task changes registration, boundary shape, third-party linking, or worker-thread behavior
2. Confirm native code is justified before coding:
   - if typed GDScript or C# is enough, escalate to `godot-architect` instead of forcing C++
   - if the task is really bounded gameplay code, route to `godot-gdscript`
   - if the workload is likely GPU-first, escalate back to `godot-architect`
3. Align the build and ABI first using `references/setup-flow.md`:
   - match the engine executable, `godot-cpp` bindings, and generated API metadata
   - match the engine's floating-point precision
   - keep debug and release outputs explicit
   - treat forward minor-version compatibility as one-way only and do not assume backward loading
4. Design the smallest safe boundary before editing code:
   - choose the lowest sane initialization level, usually `MODULE_INITIALIZATION_LEVEL_SCENE`
   - use `MODULE_INITIALIZATION_LEVEL_EDITOR` only for editor-only tools
   - prefer `RefCounted` plus `Ref<T>` for service-style processors and data containers
   - use `Node` only when scene-tree participation is truly required
   - batch data through packed arrays, resources, or coarse calls; avoid high-frequency per-element calls from GDScript
5. Implement the smallest coherent change with these immutable rules:
   - use `GDCLASS`, `_bind_methods()`, and explicit `ClassDB` registration
   - use `memnew` and `memdelete` for Godot `Object` or `Node` lifetimes
   - use `Ref<T>` for `RefCounted` and `Resource` lifetimes and boundary signatures
   - stack-allocate `Variant` values and prefer `const Variant&` or concrete Godot types at the API boundary
   - never mutate the active `SceneTree` from a worker thread
   - use `call_deferred()` for main-thread tree work or signal emission that can wake GDScript or UI
6. Use native file editing for the extension sources and any changed GDScript call sites. Call `refresh_filesystem` after changing `.gdextension` manifests, `.gd` glue code, or starter test scenes the editor must pick up before validation.
7. Read `references/examples-and-validation.md` and `references/boundary-design-checklist.md`, then validate with the strongest honest method available:
   - build success and output library mapping
   - registration shape and initialization level
   - ownership signatures and stale-pointer risks
   - thread boundaries and deferred handoff points
   - reload workflow limits, especially on Windows
   - `get_diagnostics` on any changed GDScript call sites when editor-connected validation includes script glue
8. Escalate instead of improvising:
   - broad scope or runtime-choice ambiguity -> `godot-scope` or `godot-architect`
   - the task belongs in high-level gameplay code -> `godot-gdscript`
   - risky scene or resource ownership questions -> `godot-scene-resource`
   - persistence enters the native boundary -> `godot-persistence`
   - reusable process failure in this skill -> `godot-retro`

## Output

Leave behind bounded native work with:

- files created or changed
- project surfaces inspected before editing
- sync or LSP steps used when editor-connected
- ABI and build assumptions used
- classes, methods, signals, or resources exposed across the boundary
- ownership and threading rules applied
- validation performed and the highest validation level reached
- exact blocker and next action if blocked

## Quality Gates

- The exact engine, bindings, and precision assumptions were inspected before touching build or registration files.
- The task has a real native-code justification and the C++ boundary stays narrow.
- Initialization level, class registration, and exported methods or properties are explicit.
- Editor-connected validation includes `refresh_filesystem` for changed editor-consumed files before trusting import or script state.
- Godot-managed lifetimes use `memnew` or `memdelete` and `Ref<T>` instead of ad hoc ownership patterns.
- `RefCounted` or `Resource` values crossing the GDScript boundary use `Ref<T>`, not raw pointers.
- Worker threads do not mutate the active scene tree directly.
- Boundary design favors bulk processing instead of per-node or per-element chat.
- Validation claims match the real operating mode and rebuild workflow used.

## Failure Modes

- Do not turn this skill into a generic C++ tutorial or IDE guide.
- Do not push whole gameplay systems into C++ just because native code sounds faster.
- Do not edit `.gdextension`, `SConstruct`, or bindings without checking engine-version and precision alignment first.
- Do not return raw `RefCounted*` values across the GDScript boundary.
- Do not cache raw node pointers across frees or scene changes without an explicit validity strategy.
- Do not emit signals or mutate the active scene tree directly from worker threads.
- Do not trust stale editor or script state if `.gdextension` or `.gd` files changed on disk and `refresh_filesystem` never ran.
- Do not promise hot-reload safety when class bindings changed or Windows file locks make the rebuild loop unsafe.
- Do not claim runtime or editor validation when only source review was performed.

## References

Read only as needed:

- `references/setup-flow.md`
- `references/ownership-and-lifetime.md`
- `references/boundary-design-checklist.md`
- `references/examples-and-validation.md`
- `../../foundation/Godot Language Strategy Guide.md`
