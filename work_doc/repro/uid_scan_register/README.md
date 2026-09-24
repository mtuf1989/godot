# Repro: new files found by the editor's scan get a .uid file but no registered UID

Fixed on branch `ai/agent-cli-fixes` (`editor/file_system/editor_file_system.cpp`, `ACTION_FILE_ADD`).

1. `godot --headless -e --path work_doc/repro/uid_scan_register` (the editor setting
   `interface/editor/behavior/import_resources_when_unfocused` must be on, so the scan polls).
2. Write a new script from outside: `printf 'extends Node\n' > <repro>/newguy.gd`.
3. `printf 'res://newguy.gd\n' > <repro>/probe.txt`, then wait ~3 s and read the editor's stdout.

Before the fix: `PROBE res://newguy.gd uid=uid://... registered=false load_by_uid=false`.
`newguy.gd.uid` exists on disk, but `load("uid://...")` fails in the editor and in game runs
until `EditorFileSystem.update_file()` or an editor restart.

After the fix: `registered=true load_by_uid=true`, and a separate game process resolves
the UID too (the scan's `update_cache()` now includes it).

Writing `res://x.gd` into `refresh.txt` makes the probe call `update_file()` — the old workaround.
