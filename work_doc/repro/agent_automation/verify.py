#!/usr/bin/env python3
import json, subprocess, sys, tempfile
from pathlib import Path
binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as empty:
    fixture = Path(empty)
    (fixture / "extension_list.cfg").write_text("res://poison.gdextension\n")
    (fixture / ".godot").mkdir()
    (fixture / ".godot/extension_list.cfg").write_text("res://poison.gdextension\n")
    (fixture / "project.godot").write_text('[autoload]\nPoison="*res://poison.gd"\n')
    (fixture / "poison.gd").write_text('extends Node\nfunc _init() -> void:\n\tprint("PROJECT EXECUTED")\n')
    result = subprocess.run([binary, "--headless", "--agent-capabilities"], cwd=empty, text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stderr, result.stderr
    assert "PROJECT EXECUTED" not in result.stdout
    doc = json.loads(result.stdout)
    assert doc["schema_version"] == doc["protocol_version"] == 1
    assert doc["classes"]["AudioStreamSymphony"]
    assert doc["classes"]["RTPCEngine"]
    assert len(doc["classes"]) > 100
    assert doc["build"]["architecture"] == "arm64"
    assert doc["build"]["commit"] == subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    assert "agent_tools" in doc["build"]["modules"]
    version = subprocess.check_output([binary, "--headless", "--version"], text=True).strip()
    assert version.startswith(doc["engine_version"])
    print("T2.1 project-free capability and version checks passed")
