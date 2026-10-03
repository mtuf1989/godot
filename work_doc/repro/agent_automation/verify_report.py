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
if "--faults" in sys.argv:
    for name in ("fault", "main_fault"):
        events = run("fatal_" + name, name + ".gd", ["--agent-fail-on-script-error"], expected=1, reason="script_fault")
        assert any(e.get("kind") == "SCRIPT ERROR" for e in events[:-1])
    run("fatal_parse", "parse.gd", ["--agent-fail-on-script-error"], expected=1, reason="script_fault")
    events = run("handled", "handled.gd", ["--agent-fail-on-script-error"], reason="project_exit")
    assert any(e.get("severity") == "warning" for e in events[:-1])
    assert any("handled application error" in e.get("message", "") for e in events[:-1])
    # Flag-free faults remain bounded only by --quit-after; no policy leaks.
    result = subprocess.run([binary, "--headless", "--path", str(project), "--script", "fault.gd", "--quit-after", "4"], text=True, capture_output=True, timeout=15)
    assert result.returncode == 0, result.stderr
    # The policy also works without a report file.
    result = subprocess.run([binary, "--headless", "--path", str(project), "--script", "fault.gd", "--agent-fail-on-script-error"], text=True, capture_output=True, timeout=15)
    assert result.returncode == 1 and "SCRIPT ERROR" in result.stderr
    try:
        subprocess.run([binary, "--headless", "--path", str(project), "--script", "clean.gd", "--agent-fail-on-script-error"], capture_output=True, timeout=1)
        raise AssertionError("unrelated hang unexpectedly terminated")
    except subprocess.TimeoutExpired:
        pass
    print("T2.3 opt-in script-fault and nonfatal-policy checks passed")
