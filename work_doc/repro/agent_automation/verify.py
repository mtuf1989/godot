#!/usr/bin/env python3
import json, subprocess, sys, tempfile
from pathlib import Path
binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as empty:
    result = subprocess.run([binary, "--headless", "--agent-capabilities"], cwd=empty, text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    doc = json.loads(result.stdout)
    assert doc["schema_version"] == doc["protocol_version"] == 1
    assert doc["classes"]["AudioStreamSymphony"]
    assert doc["build"]["architecture"] == "arm64"
    assert doc["build"]["commit"] == subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    assert "agent_tools" in doc["build"]["modules"]
    version = subprocess.check_output([binary, "--headless", "--version"], text=True).strip()
    assert version.startswith(doc["engine_version"])
    print("T2.1 project-free capability and version checks passed")
