# Automation patch ledger

Audit base: `ab699d8ccdb2e3afc79d52a94c729e5e2f872aec`.
Branch: `ai/native-agent-automation`. Protocol and report schema: **1**.

| Task | Native interface | Introduction commit | Reproduction |
|---|---|---|---|
| T2.1 | `--agent-capabilities` | `289a7147d1` | `repro/agent_automation/verify.py` |
| T2.2 | `--agent-report <path>` | `21e409c507` | `repro/agent_automation/verify_report.py`, native AgentTools tests |
| T2.3 | `--agent-fail-on-script-error` | `48b45a3c0a` | `verify_report.py --faults` |
| T2.4 | `--agent-strict-resources` | `3212b34e32` | `verify_strict.py`, engine-produced round trips |
| T2.1 follow-up | Project-independent extension loading; full registered class inventory | `d146bc379d` | Poisoned root/.godot extension lists and autoloads in `verify.py` |

All behavior-changing interfaces are opt-in. Module-disabled builds reject their
flags explicitly. Main-thread report finalization synchronizes callback and
strict-hook producers before snapshotting the exit. Bounded evidence loss is
reported and causes nonzero exit. Diagnostics cover setup through main-loop
finalization, excluding the engine teardown interval where ordinary Godot
printing disables error-handler delivery. JSON location fields preserve unknown
lines and distinguish native source from project paths.

The complete reproduction commands and protocol boundaries are in
[repro/agent_automation/README.md](repro/agent_automation/README.md).
The template's `SESSION_PLAN.md` records the actual completed candidate build
revision and validation gate. Release packaging/integration belongs to Batch 3.
