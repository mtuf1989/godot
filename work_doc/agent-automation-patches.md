# Automation patch ledger

Audit base: `ab699d8ccdb2e3afc79d52a94c729e5e2f872aec`.
Branch: `ai/native-agent-automation`. Every introduction commit includes its task ID.

| Task | Native interface | Evidence |
|---|---|---|
| T2.1 | `--agent-capabilities`, schema/protocol 1 | `repro/agent_automation/verify.py` |
| T2.2 | `--agent-report <path>`, JSONL schema 1 | `repro/agent_automation/verify_report.py`, native AgentTools tests |
