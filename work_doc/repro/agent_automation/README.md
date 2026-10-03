# Native automation patches

Protocol 1, opt-in module `agent_tools` (disable with `module_agent_tools_enabled=no`).
`--agent-capabilities` emits one JSON document after native class registration, without
loading project data, project extensions, autoloads, or a main loop. It uses dummy
rendering/audio. `build` records source HEAD, dirty state, platform, architecture,
target, enabled modules, and selected SCons options. Dirty builds are development
builds, not reproducible release pins; Batch 3 adds artifact manifests/checksums.

Run `python3 work_doc/repro/agent_automation/verify.py bin/godot.macos.editor.arm64`.

## Report contract (T2.2)

`--agent-report <filesystem-path>` writes JSONL schema 1. Relative paths resolve
against the invocation working directory before `--path` processing; use an
absolute path in automation. One `diagnostic` per observed engine error callback,
then one terminal `run_summary`. Ordinary logs remain untouched. Diagnostics keep
`source_location`, and separate `project_location` (`res://`/`user://`) from
`engine_location` (relative native source). Absolute/embedded locations are not
guessed into project or engine locations. Unknown lines/locations are JSON null.

Coverage is setup through main-loop finalize, ending before engine teardown
(where ordinary Godot disables error-handler printing). A killed/crashed process
can lack the final summary; consumers must fail that run. Iterations count calls
that reach the end of the main loop, not render frames or physics ticks. A
project-requested exit takes precedence when it coincides with the budget.
`script_fault_count` records faults even when report-only mode leaves execution
unchanged. Startup failures and runs without a loop report zero iterations.

The callback queue holds 256 bounded diagnostics. Overflow, text truncation,
or reentrant raw-fallback errors increment loss counters and invalidate
`evidence_complete`; the process exits nonzero. Report open/write/close failures
also exit nonzero. Handlers are removed under the engine error lock before the
last drain. No error callback does engine allocation, file I/O, or scene mutation.

Run `python3 work_doc/repro/agent_automation/verify_report.py <binary> <output-dir>`
and native `--headless --test --test-case='*AgentTools*'`.

## Script fault policy (T2.3)

`--agent-fail-on-script-error` marks `ERR_HANDLER_SCRIPT` faults for safe nonzero
exit at the next main-loop boundary (or report finalization for startup faults).
It also works without `--agent-report`. It never quits or mutates the SceneTree
inside an error callback. Parse/compile and VM execution faults are fatal;
`push_warning`, `push_error`, and ordinary native API errors retain their normal
policies. This flag does not detect infinite loops or unrelated hangs; retain
wrapper timeouts. Flag-free runs are unchanged.

`run_summary.reason = script_fault` identifies policy termination and takes
precedence over a coincident budget or project exit. Run `verify_report.py
<binary> <output-dir> --faults` for both initialization and main-loop faults.

## Strict assignment policy (T2.4)

`--agent-strict-resources` checks actual `Object::set` validity at text resource
assignments (including subresources and PackedScene-resource metadata), ordinary
scene properties, inherited/instance overrides, and deferred NodePath setters.
Only a confirmed rejection produces `kind = rejected_property`, with resource,
node path when applicable, property, and phase. Serialized property lines are
unknown at instantiation; line remains null. Missing resources intentionally
preserved as metadata and old script-state restoration are excluded: no actual
serialized setter rejection occurred there. Existing script assignment errors
retain the ordinary engine diagnostic path.

This does not validate all semantic property values or missing NodePath targets.
Dynamic `_set`, metadata, and native dynamic resources use their real setters.
Text scene loading stores node properties; validation of those properties needs
`PackedScene.instantiate()`, which runs constructors and setters even outside the
SceneTree. Runtime wiring still needs a smoke test. A strict rejection safely
exits nonzero, with reason `resource_validation_failure`, even without a report.
Flag-free loading/instantiation keeps baseline behavior.

Strict reproduction command (also uses the template `gd` CLI):

```sh
python3 work_doc/repro/agent_automation/verify_strict.py bin/godot.macos.editor.arm64 /tmp/agent-strict ../game-template /tmp/godot-batch2-baseline
```

The final optional argument is a preserved flag-free baseline binary. This checks
invalid resource/subresource/scene/inherited/export/deferred assignments, actual
engine-saved dynamic resources and node references, load-only versus instantiate
coverage, and a real SceneTree boot. Native tests cover missing-resource metadata
preservation, worker-thread loading, queue overflow, text truncation, reentrancy,
and repeated handler lifecycle. Buffers allocate only when reporting is enabled;
finalization stops both callback and direct strict-hook producers before taking
the exit snapshot. Raw logs and JSONL files stay in the selected output directory.
