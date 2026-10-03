# Native automation patches

Protocol 1, opt-in module `agent_tools` (disable with `module_agent_tools_enabled=no`).
`--agent-capabilities` emits one JSON document after native class registration, without
loading project data, project extensions, autoloads, or a main loop. It uses dummy
rendering/audio. `build` records source HEAD, dirty state, platform, architecture,
target, enabled modules, and selected SCons options. Dirty builds are development
builds, not reproducible release pins; Batch 3 adds artifact manifests/checksums.

Run `python3 work_doc/repro/agent_automation/verify.py bin/godot.macos.editor.arm64`.
