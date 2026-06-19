---
name: godot-context
description: |
  Maintain the project's lightweight context files (PROJECT.md and SESSION.md) so LLM agents can orient quickly across sessions.
  Use when the user asks to document a system, update the project map, record what was done this session, prepare a session handoff, or when a structural change (new system, renamed signals, new autoload) means PROJECT.md is stale.
  Also use when the user says "update context", "what systems exist", "write a session summary", "what did we do", "wrap up the session", or "hand off".
  Do not use for MEMORY.md — that is owned by godot-retro.
---

# Godot Context

Maintain two flat context files at the project root so any LLM agent can load project understanding in one pass.

## Files

| File | Purpose | Who writes | When |
|---|---|---|---|
| `PROJECT.md` | System map, connections, file locations | LLM after structural changes | When systems are added, renamed, connected, or removed |
| `SESSION.md` | Session continuity and handoff | LLM at session end | End of every working session, or when the user asks |

These files are read at session start via the Context Loading Protocol in `GODOT-PROTOCOL.md`. This skill handles *writing* them.

## PROJECT.md Maintenance

### When to update

- A new system, autoload, or major subsystem is created
- Signals between systems change (new connections, renamed signals, removed events)
- Key file paths move or are restructured
- A system's status changes (draft → active, active → deprecated)

### How to update

1. Read the current `PROJECT.md`.
2. Read the actual source files that changed to get ground truth.
3. Update only the sections that changed. Do not rewrite unrelated sections.
4. Keep entries short — purpose, key files, signals in/out, dependencies, status, notes. No implementation code.

### Rules

- One section per system under `## Systems`.
- `## Cross-System Events` shows signal flows between systems, not internal signals.
- `## Autoloads` lists each autoload with a one-line purpose.
- If a system is too small to warrant its own section, it does not need one yet.
- Do not invent systems that do not exist in the codebase. Inspect before documenting.

## SESSION.md Maintenance

### When to write

- The user asks to wrap up, hand off, or summarize the session.
- The session is ending naturally (user says goodbye, last task is done).
- The user explicitly asks for a session summary.

### How to write

1. Replace the entire content of `SESSION.md` — it is not append-only. Only the last session matters.
2. Use this structure:

```markdown
# Last Session: YYYY-MM-DD

## What we did
- brief list of what was accomplished

## What's done
- concrete deliverables completed

## What's next
- immediate next steps

## What's broken / blocked
- known issues or blockers

## PROJECT.md changes needed
- any pending updates to the project map (if not already applied)
```

3. Keep it under 30 lines. This is a handoff note, not a journal.

### Rules

- Do not write SESSION.md mid-session unless the user asks. It captures the *end state*.
- Do not include implementation details — just what, not how.
- If nothing is broken or blocked, omit that section rather than writing "none".

## What this skill does NOT do

- **MEMORY.md** — owned by `godot-retro`. This skill reads it but never writes it.
- **Deep documentation** — no wiki pages, no ontology graphs, no tiered folder structures. If the user needs detailed design docs, those go in `docs/` as standalone files, not managed by this skill.
- **Code analysis** — this skill documents what exists, it does not analyze code quality or suggest improvements.

## Quality Gates

- PROJECT.md reflects the actual codebase, not aspirational design.
- SESSION.md is concise enough to read in one pass (<30 lines).
- No section in PROJECT.md describes a system that does not exist in the project.
- Cross-System Events only lists signals that actually cross system boundaries.
