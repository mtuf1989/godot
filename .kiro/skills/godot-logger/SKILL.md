---
name: godot-logger
description: |
  Implement structured session logging in Godot 4.6+ using the GLog addon — a lightweight,
  LLM-optimized logger with severity levels, sampling, state transitions, and automatic
  engine error capture.
  Use when the user needs to add event logging to gameplay systems, instrument state machines,
  track game events to structured JSONL files, configure GLog severity/sampling/rate-limiting,
  manage log sessions, export session logs for LLM debugging, or integrate the GLog plugin.
  Also use when someone mentions GLog, GLog.info, GLog.event, GLog.transition, GLog.error,
  prepare_for_llm, session_started, session_stopped, glog_config, events.jsonl, session_meta,
  severity levels, sampling constants, SAMPLE_60, min_severity, rate_limit, buffer_capacity,
  or asks to "add logging," "log this event," "track player actions," "instrument this system,"
  "debug log," "export logs for LLM," "set up logging," or "log state transitions."
  Even if the user just says "add logging to this system" or "I need to track what happens
  when the player takes damage" or "log game events" in a Godot project, this skill applies.
  Do NOT use for general GDScript unrelated to logging (use godot-gdscript), audio playback
  (use godot-sound), or game feel/juice (use godot-feel).
---

# Godot Logger (GLog)

Implement structured session logging using the GLog addon with correct API usage, severity levels, sampling, and session-aware patterns.

Read `references/api-reference.md` before writing any GLog calls.

## Responsibility

- Wire `GLog.info()` / `GLog.warn()` / `GLog.error()` / `GLog.debug()` calls to log structured game events.
- Use `GLog.transition()` for FSM state changes.
- Configure GLog via `GLog.config.*` overrides when defaults don't fit.
- Manage session lifecycle (`GLog.start_session()`, `GLog.stop_session()`) when autostart is disabled.
- Use sampling (`GLog.SAMPLE_60`, etc.) for high-frequency events.
- Use lazy state (`Callable` in the `state` parameter) for expensive snapshots.
- Export session logs for LLM debugging via `GLog.prepare_for_llm()`.

## Do Not Use When

- General GDScript unrelated to logging → `godot-gdscript`.
- Architecture decisions about what to log → `godot-architect`.
- Persisting game state to save files → `godot-persistence`.
- Audio, UI, shaders, or other domains with their own skills.
- The user only wants `print()` output with no structured logging.

## Core Principle: The Log Is the Narrative

GLog exists so that after a play session, reading only the log reconstructs the player's experience — like a transcript of a text adventure. The foundation Operating Model (section 6.5) requires every gameplay system to include GLog instrumentation. Logging is part of the definition of done.

The bar: *could you reconstruct what the player experienced from this log alone?*

Log **actions** (player did X), **decisions** (AI chose Y because Z), **transitions** (state A→B), and **outcomes** (player died, wave cleared). Don't log raw data dumps — log the story.

## Core Mental Model

GLog is a zero-game-thread-I/O structured logger. Events are buffered in memory and flushed to JSONL by a background thread.

```
GLog (autoload singleton)
  ├── config: GLogConfig         (severity gate, rate limit, buffer size, etc.)
  ├── session lifecycle           (start/stop, 5-char SID, auto-cleanup)
  ├── ring buffer + flush thread  (mutex-guarded swap, background JSON serialization)
  ├── GLogBridge                  (auto-captures push_error/push_warning)
  └── GLogLLM                    (priority-filtered export for LLM context windows)
```

Key concepts:
- **Severity levels**: DEBUG(0) < INFO(1) < WARN(2) < ERROR(3). Default gate is INFO — DEBUG is dropped in production.
- **Categories**: Free-form string tags (e.g., `"combat"`, `"ui"`, `"state"`). No registration needed.
- **Sessions**: Bounded logging periods with a 5-char SID. Auto-start by default. All events belong to exactly one session.
- **Sampling**: Per-event-name counter with modulo. Use for per-frame data to reduce volume.
- **Lazy state**: Pass a `Callable` as the `state` parameter — executed on the flush thread, zero game-thread cost.
- **Engine error capture**: `push_error()` and `push_warning()` are auto-captured as structured events. Bypasses all filters.
- **LLM export**: `prepare_for_llm()` produces a priority-filtered string sized for LLM context windows.

## Workflow

1. Confirm GLog addon is present (`addons/GLog/plugin.cfg`). Check that the `GLog` autoload is registered. If missing:
   - Copy `addons/GLog/` into the project.
   - Enable the plugin in Project Settings → Plugins.
   - The plugin auto-registers the `GLog` autoload.

2. Read `references/api-reference.md` for exact method signatures before writing any calls.

3. Identify what the task requires and write the appropriate calls:

   **Basic event logging:**
   ```gdscript
   GLog.info("player_spawned", "gameplay", {"position": player.position})
   GLog.warn("low_health", "combat", {"hp": 5, "max_hp": 100})
   GLog.error("null_reference", "inventory", {"item_id": "sword_01"})
   ```

   **State transitions:**
   ```gdscript
   GLog.transition("Player", "idle", "running", "input_move")
   ```

   **High-frequency events with sampling:**
   ```gdscript
   GLog.debug("position", "movement", {"pos": global_position}, null, GLog.SAMPLE_60)
   ```

   **Lazy state for expensive snapshots:**
   ```gdscript
   GLog.info("wave_complete", "gameplay", {"wave": idx}, func():
       return {"enemies_alive": _count_alive(), "player_hp": player.hp}
   )
   ```

   **Session metadata:**
   ```gdscript
   GLog.set_session_meta("current_level", "dungeon_03")
   ```

   **Manual session control (when autostart is disabled):**
   ```gdscript
   GLog.config.autostart = false  # in _ready() before first _process()
   GLog.start_session({"level": "tutorial", "build": "0.4.2"})
   # ... gameplay ...
   GLog.stop_session()
   ```

   **LLM export:**
   ```gdscript
   GLog.stop_session()
   var log_text: String = GLog.prepare_for_llm()
   DisplayServer.clipboard_set(log_text)
   GLog.start_session()
   ```

   **Config overrides (in another autoload's `_ready()`):**
   ```gdscript
   GLog.config.min_severity = GLog.DEBUG
   GLog.config.rate_limit = 500
   GLog.config.buffer_capacity = 4096
   ```

4. Write files to disk using native file editing. Call `refresh_filesystem` after writing `.gd` files.

5. Validate:
   - Use `get_diagnostics` on changed scripts. `GLog` is a global singleton — no import needed.
   - Confirm category strings are consistent across the codebase.
   - If a test scene exists, use `run_scene` to verify no runtime errors.

6. Escalate if needed:
   - What events to log, logging strategy → `godot-architect`
   - Save/load of game state (not logs) → `godot-persistence`
   - General GDScript outside logging → `godot-gdscript`

## Quality Gates

- All calls use correct GLog signatures: `GLog.info(event_name, cat, data, state, sample)`, NOT `GLog.info("message")`.
- Every event has both an `event_name` and a `cat` — these are required positional args.
- `GLog.error()` is never called with a `sample` parameter (it doesn't accept one).
- Sampling is used for per-frame or high-frequency events (physics ticks, position tracking).
- Lazy state uses `Callable` in the `state` parameter (5th arg), not in `data`.
- `transition()` data dict avoids reserved keys: `entity`, `from`, `to`, `trigger`.
- Config overrides happen in `_ready()` before the first `_process()` frame.
- Typed GDScript throughout (variables, parameters, return types).
- `refresh_filesystem` is called after disk writes before editor validation.

## Failure Modes

- Do NOT call `GLog.event()` with a string severity — severity is an `int` constant (`GLog.INFO`, etc.).
- Do NOT pass nested mutable dicts in `data` without `data.duplicate(true)` — shallow copy can leak mutations.
- Do NOT access scene tree nodes inside a lazy state `Callable` — it runs on the flush thread.
- Do NOT expect `GLog.prepare_for_llm()` to work on an active session — stop the session first.
- Do NOT log in any autoload's `_ready()` when autostart is enabled — the session hasn't started yet (it starts on first `_process()`).
- Do NOT assume `GLog.error()` accepts a `sample` parameter — it intentionally omits it.
- Do NOT claim validation that was not performed.

## References

Read only as needed:

- `references/api-reference.md` — complete API surface, signals, constants, config, disk format, synthetic events
- `../../foundation/Godot Nuanced Development Practices.md`
