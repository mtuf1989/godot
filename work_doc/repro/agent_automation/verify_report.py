#!/usr/bin/env python3
"""Real-process regression cases; disposable project and retained JSONL/raw logs."""
import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
binary = str(Path(sys.argv[1]).resolve())
output = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path(tempfile.mkdtemp(prefix="agent-evidence-"))
output.mkdir(parents=True, exist_ok=True)
project = output / "fixture"
project.mkdir(exist_ok=True)
(project / "project.godot").write_text('[application]\nconfig/name="Agent automation fixture"\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n')
scripts = {
    "clean.gd": 'extends SceneTree\nfunc _initialize() -> void:\n\tprint("clean started")\n',
    "early.gd": 'extends SceneTree\nfunc _initialize() -> void:\n\tquit(7)\n',
    "parse.gd": 'extends SceneTree\nfunc broken(\n',
    "fault.gd": 'extends SceneTree\nfunc _initialize() -> void:\n\tvar target: Object = null\n\ttarget.call("missing")\n\tquit()\n',
    "main_fault.gd": 'extends SceneTree\nfunc _process(_delta: float) -> bool:\n\tvar target: Object = null\n\ttarget.call("missing")\n\treturn false\n',
    "worker.gd": 'extends SceneTree\nvar worker := Thread.new()\nfunc _initialize() -> void:\n\tworker.start(_worker)\n\tworker.wait_to_finish()\nfunc _worker() -> void:\n\tpush_error("worker fixture diagnostic")\n',
    "handled.gd": 'extends SceneTree\nfunc _initialize() -> void:\n\tpush_warning("intentional warning")\n\tpush_error("handled application error")\n\tquit()\n',
}
for name, text in scripts.items():
    (project / name).write_text(text)

def run(name, script, extra=(), expected=0, reason=None):
    report = output / (name + ".jsonl")
    args = [binary, "--headless", "--path", str(project), "--agent-report", str(report), "--script", script, *extra]
    result = subprocess.run(args, text=True, capture_output=True, timeout=15)
    (output / (name + ".log")).write_text(result.stdout + result.stderr)
    assert result.returncode == expected, (name, result.returncode, result.stderr)
    events = [json.loads(line) for line in report.read_text().splitlines()]
    assert events[-1]["event"] == "run_summary", events
    summary = events[-1]
    assert summary["exit_code"] == expected, (name, summary)
    assert summary["evidence_complete"], (name, summary)
    if reason:
        assert summary["reason"] == reason, (name, summary)
    assert summary["diagnostic_count"] == len(events) - 1
    return events

events = run("budget", "clean.gd", ["--quit-after", "4", "--fixed-fps", "60"], reason="iteration_budget")
assert events[-1]["observed_iterations"] == 4
assert events[-1]["requested_iterations"] == 4
run("early", "early.gd", ["--quit-after", "40"], expected=7, reason="project_exit")
run("parse", "parse.gd", ["--quit-after", "4"], expected=1, reason="startup_failure")
events = run("fault", "fault.gd", ["--quit-after", "4"], reason="iteration_budget")
assert any(e.get("kind") == "SCRIPT ERROR" and e["project_location"]["path"] == "res://fault.gd" for e in events[:-1])
events = run("worker", "worker.gd", ["--quit-after", "4"], reason="iteration_budget")
assert any(e.get("worker_thread") and "worker fixture diagnostic" in e["message"] for e in events[:-1])
# Native reporting must leave raw evidence available.
assert "worker fixture diagnostic" in (output / "worker.log").read_text()
print("T2.2 process/report checks passed; artifacts:", output)
