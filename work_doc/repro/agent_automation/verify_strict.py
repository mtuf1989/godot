#!/usr/bin/env python3
import json, os, shutil, subprocess, sys
from pathlib import Path
binary = str(Path(sys.argv[1]).resolve())
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
project = output / "fixture"
project.mkdir(exist_ok=True)
source = Path(__file__).parent / "fixtures"
for path in source.glob("*.gd"):
    shutil.copyfile(path, project / path.name)
(project / "project.godot").write_text('[application]\nconfig/name="Agent strict fixture"\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n')
template = Path(sys.argv[3]).resolve()
shutil.copytree(template / "addons/agent_cli", project / "addons/agent_cli", dirs_exist_ok=True)
env = dict(os.environ, GODOT_BIN=binary)
gd = project / "addons/agent_cli/gd.mjs"
result = subprocess.run(["node", str(gd), "check", "node.gd", "factory.gd", "loader.gd", "--json"], cwd=project, env=env, text=True, capture_output=True, timeout=120)
(output / "gd-check.log").write_text(result.stdout + result.stderr)
assert result.returncode == 0, result.stdout + result.stderr
result = subprocess.run(["node", str(gd), "run", "factory.gd", "--json"], cwd=project, env=env, text=True, capture_output=True, timeout=120)
(output / "factory.log").write_text(result.stdout + result.stderr)
assert result.returncode == 0, result.stdout + result.stderr

bad = {
    "bad.tres": '[gd_resource type="Resource" format=3]\n[resource]\nposiiton = Vector2(1, 2)\n',
    "bad_sub.tres": '[gd_resource type="Theme" load_steps=2 format=3]\n[sub_resource type="StyleBoxFlat" id="Bad"]\nposiiton = Vector2(1, 2)\n[resource]\nButton/styles/normal = SubResource("Bad")\n',
    "bad.tscn": '[gd_scene format=3]\n[node name="Broken" type="Node2D"]\nposiiton = Vector2(1, 2)\n',
    "base.tscn": '[gd_scene format=3]\n[node name="Base" type="Node2D"]\nposition = Vector2(1, 2)\n',
    "inherited.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="PackedScene" path="res://base.tscn" id="1"]\n[node name="Derived" instance=ExtResource("1")]\nposiiton = Vector2(1, 2)\n',
    "bad_export.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://node.gd" id="1"]\n[node name="BadExport" type="Node2D"]\nscript = ExtResource("1")\nhealht = 9\n',
    "bad_deferred.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://node.gd" id="1"]\n[node name="BadDeferred" type="Node2D" node_paths=PackedStringArray("targte")]\nscript = ExtResource("1")\ntargte = NodePath("Child")\n[node name="Child" type="Node2D" parent="."]\n',
}
for name, text in bad.items():
    (project / name).write_text(text)

def run(name, resources, strict=True, expected=0, baseline=None, load_only=False):
    report = output / (name + ".jsonl")
    args = [baseline or binary, "--headless", "--path", str(project), "--script", "loader.gd"]
    if not baseline:
        args += ["--agent-report", str(report)]
    if strict:
        args += ["--agent-strict-resources"]
    args += ["--", *["res://" + value for value in resources], *(["load_only"] if load_only else [])]
    result = subprocess.run(args, text=True, capture_output=True, timeout=15)
    (output / (name + ".log")).write_text(result.stdout + result.stderr)
    assert result.returncode == expected, (name, result.returncode, result.stdout, result.stderr)
    if baseline:
        return []
    events = [json.loads(line) for line in report.read_text().splitlines()]
    summary = events[-1]
    assert summary["event"] == "run_summary" and summary["exit_code"] == expected, (name, summary)
    assert summary["evidence_complete"], (name, summary)
    rejected = [e for e in events if e.get("kind") == "rejected_property"]
    if expected:
        assert summary["reason"] == "resource_validation_failure", (name, summary)
        assert rejected, events
        for event in rejected:
            assert event["resource"].startswith("res://"), event
            assert event["project_location"]["line"] is None and event["engine_location"] is None, event
        assert "Rejected serialized property" in result.stderr
    else:
        assert not rejected, rejected
    return rejected

valid = ["valid.tscn", "theme.tres", "material.tres", "animation.tres", "symphony.tres", "sound_event.tres"]
run("roundtrips", valid)
for index, name in enumerate(["bad.tres", "bad_sub.tres", "bad.tscn", "inherited.tscn", "bad_export.tscn", "bad_deferred.tscn"]):
    rejected = run("strict_" + str(index), [name], expected=1)
    assert rejected[0]["property"] in ["posiiton", "healht", "targte"]
    if name.endswith(".tscn"):
        assert rejected[0]["node"] is not None, rejected
    run("flagfree_" + str(index), [name], strict=False)
    if len(sys.argv) > 4:
        run("baseline_" + str(index), [name], strict=False, baseline=str(Path(sys.argv[4]).resolve()))
# Loading PackedScene stores properties; only instantiation attempts setters.
run("scene_load_only", ["bad.tscn"], load_only=True)
# Plain valid resources remain equivalent without flags.
run("flagfree_valid", valid, strict=False)
if len(sys.argv) > 4:
    run("baseline_valid", valid, strict=False, baseline=str(Path(sys.argv[4]).resolve()))
# Real SceneTree boots the generated composed scene through gd.
result = subprocess.run(["node", str(gd), "smoke", "valid.tscn", "--frames", "4", "--json"], cwd=project, env=env, text=True, capture_output=True, timeout=120)
(output / "gd-smoke.log").write_text(result.stdout + result.stderr)
assert result.returncode == 0, result.stdout + result.stderr
print("T2.4 strict/flag-free assignments and engine-produced round trips passed; artifacts:", output)
