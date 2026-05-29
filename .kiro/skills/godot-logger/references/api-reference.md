# GLog API Reference

> Read this file before writing any GLog calls. Ground truth for method signatures, signals, constants, configuration, and disk format.

## Autoload Singleton

GLog registers as autoload `GLog` via `plugin.gd`. Globally available — no imports needed.

## Severity Constants

| Constant    | Value | Use for                                      |
|-------------|-------|----------------------------------------------|
| `GLog.DEBUG`| `0`   | Per-frame noise, physics ticks, verbose traces |
| `GLog.INFO` | `1`   | Narrative backbone: actions, decisions, transitions |
| `GLog.WARN` | `2`   | Unexpected but recoverable                   |
| `GLog.ERROR`| `3`   | Something broke — triggers immediate flush   |

Default `min_severity` is `1` (INFO). DEBUG events are dropped in production.

## Sampling Constants

| Constant        | Value | Meaning                    |
|-----------------|-------|----------------------------|
| `GLog.SAMPLE_10`| `10`  | Log 1 in every 10 calls   |
| `GLog.SAMPLE_30`| `30`  | Log 1 in every 30 calls   |
| `GLog.SAMPLE_60`| `60`  | Log 1 in every 60 calls   |
| `GLog.SAMPLE_120`|`120` | Log 1 in every 120 calls  |

Any positive integer works as a custom sample rate.

## Event Logging

### GLog.event()

```gdscript
GLog.event(event_name: String, cat: String, severity: int,
           data: Dictionary = {}, state: Variant = null, sample: int = 0) -> void
```

Core method — all convenience wrappers delegate to this.

| Parameter    | Type       | Default | Description                                                |
|--------------|------------|---------|------------------------------------------------------------|
| `event_name` | `String`   | —       | Event identifier (e.g., `"damage_taken"`)                  |
| `cat`        | `String`   | —       | Category tag (e.g., `"combat"`, `"ui"`)                    |
| `severity`   | `int`      | —       | `GLog.DEBUG`, `INFO`, `WARN`, or `ERROR`                   |
| `data`       | `Dictionary`| `{}`   | Flat key-value payload. Shallow-copied internally.         |
| `state`      | `Variant`  | `null`  | Game state snapshot or `Callable` for lazy evaluation.     |
| `sample`     | `int`      | `0`     | Sample rate. `0` = log every call. `60` = 1-in-60.        |

Behavior:
- Returns silently if no session is active.
- Drops events below `config.min_severity`.
- Rate-limited globally (default 200 events/sec).
- `data` is shallow-copied — flat dicts are safe to reuse. Nested mutable dicts need `data.duplicate(true)` at call site.
- `state` as `Callable` is deferred to the flush thread (zero game-thread cost). The Callable runs off the main thread — don't access scene tree nodes.

### Convenience Wrappers

```gdscript
GLog.debug(event_name: String, cat: String, data: Dictionary = {}, state: Variant = null, sample: int = 0) -> void
GLog.info(event_name: String, cat: String, data: Dictionary = {}, state: Variant = null, sample: int = 0) -> void
GLog.warn(event_name: String, cat: String, data: Dictionary = {}, state: Variant = null, sample: int = 0) -> void
GLog.error(event_name: String, cat: String, data: Dictionary = {}, state: Variant = null) -> void
```

`error()` has NO `sample` parameter — errors always log and trigger immediate flush.

## State Transitions

### GLog.transition()

```gdscript
GLog.transition(entity: String, from_state: String, to_state: String,
                trigger: String = "", data: Dictionary = {}) -> void
```

Logs at INFO severity with `cat = "state"`, `event = "state_transition"`. Adds `entity`, `from`, `to`, `trigger` keys to `data` (overwrites if present, emits `push_warning()`).

## Session Lifecycle

### GLog.start_session()

```gdscript
GLog.start_session(meta: Dictionary = {}) -> void
```

Generates a 5-char alphanumeric SID, creates session directory, starts flush thread, registers engine error bridge. Emits `session_started(sid)`. Warns and returns if already active.

### GLog.stop_session()

```gdscript
GLog.stop_session() -> void
```

Injects `session_stop` synthetic event, drains buffer, writes `session_meta.json`, appends to `sessions_index.jsonl`, cleans up old sessions. Emits `session_stopped(sid)`. Returns silently if no session active.

### GLog.set_session_meta()

```gdscript
GLog.set_session_meta(key: String, value: Variant) -> void
```

Attaches metadata to the current session. Warns if no session active.

## Signals

| Signal                          | Emitted When                |
|---------------------------------|-----------------------------|
| `session_started(sid: String)`  | Session begins              |
| `session_stopped(sid: String)`  | Session ends                |

## LLM Export

```gdscript
GLog.prepare_for_llm(sid: String = "", token_budget: int = 0) -> String
GLog.prepare_for_llm_file(path: String, sid: String = "", token_budget: int = 0) -> void
```

Reads completed session JSONL and produces priority-filtered output sized for LLM context. Empty `sid` = most recent session. `0` budget = `config.llm_token_budget`.

Selection priority (when budget is tight):
1. Session meta header (~200 chars, always)
2. All ERROR and WARN events (100%)
3. All `state_transition` events (100%)
4. Trailing N events before each error (context window)
5. First/last 5 events as bookends
6. Evenly-sampled INFO events to fill remaining budget

**Only works on completed sessions** — call `stop_session()` first or wait for a flush cycle.

## Configuration

All config in `glog_config.gd` as plain GDScript vars on `GLogConfig`. Override at runtime via `GLog.config`:

```gdscript
GLog.config.min_severity = GLog.DEBUG
GLog.config.rate_limit = 500
```

| Variable              | Type   | Default          | Purpose                                    |
|-----------------------|--------|------------------|--------------------------------------------|
| `min_severity`        | `int`  | `1` (INFO)       | Severity gate threshold                    |
| `buffer_capacity`     | `int`  | `2048`           | Ring buffer max events                     |
| `flush_interval_ms`   | `int`  | `200`            | Background flush period (ms)               |
| `rate_limit`          | `int`  | `200`            | Max events per second (global)             |
| `max_file_size_mb`    | `int`  | `50`             | Session file size cap                      |
| `base_dir`            | `String`| `"user://logs/"`| Log root directory                         |
| `error_context_window`| `int`  | `10`             | Events before each error in LLM export     |
| `llm_token_budget`    | `int`  | `32000`          | Default LLM export character budget        |
| `autostart`           | `bool` | `true`           | Auto-start session on first `_process()`   |
| `max_sessions`        | `int`  | `20`             | Max session dirs on disk                   |

Override config in another autoload's `_ready()` — autostart is deferred to first `_process()` frame.

## Event Envelope (on disk)

```json
{"t":1714150000.0,"frame":1842,"sid":"a1b2c","cat":"combat","event":"damage_taken","severity":"info","data":{"source":"Goblin","amount":26}}
```

| Field      | Type       | Description                              |
|------------|------------|------------------------------------------|
| `t`        | `float`    | Unix timestamp                           |
| `frame`    | `int`      | Engine frame number                      |
| `sid`      | `String`   | Session ID                               |
| `cat`      | `String`   | Category tag                             |
| `event`    | `String`   | Event name                               |
| `severity` | `String`   | `"debug"`, `"info"`, `"warn"`, `"error"` |
| `data`     | `Dictionary`| Payload (omitted if empty)              |
| `state`    | `Variant`  | State snapshot (omitted if null)         |

## Disk Structure

```
user://logs/
├── sessions_index.jsonl      ← one line per session
├── a1b2c/
│   ├── events.jsonl          ← all events, one JSON object per line
│   └── session_meta.json     ← summary stats, error list, metadata
```

### session_meta.json

Written on `stop_session()`. Contains: `sid`, `started_at`, `ended_at`, `duration_sec`, `event_count`, `events_by_category`, `events_by_severity`, `error_events`, `clean_shutdown`, `events_dropped`, `rate_limited_count`, `buffer_overflows`, `coverage`, `meta`.

`coverage` = ratio of events logged vs. total attempted. Below 1.0 = data loss.

## Synthetic Events (category `_glog`)

| Event              | Severity | Meaning                                    |
|--------------------|----------|--------------------------------------------|
| `session_start`    | INFO     | Session began                              |
| `session_stop`     | INFO     | Clean shutdown                             |
| `_rate_limited`    | WARN     | Events dropped — logging too fast          |
| `_buffer_overflow` | WARN     | Buffer full, events evicted                |
| `_backpressure`    | WARN     | Flush thread fell behind                   |
| `_file_size_cap`   | WARN     | File hit `max_file_size_mb`, writes stopped|

## Engine Error Capture

GLog auto-captures `push_error()`, `push_warning()`, script errors, and shader errors via `GLogBridge` (Godot Logger API). These appear as `cat="_engine"`, `event="engine_error"`. They bypass severity gate, rate limiter, and sampling.

## Recommended Categories

| Category     | Use for                                    |
|--------------|--------------------------------------------|
| `"combat"`   | Damage, kills, abilities, buffs            |
| `"movement"` | Position, velocity, collisions             |
| `"state"`    | FSM transitions (used by `transition()`)   |
| `"ui"`       | Menu opens, button clicks, screen changes  |
| `"economy"`  | Purchases, currency changes, loot          |
| `"inventory"`| Item pickups, drops, equipment changes     |
| `"ai"`       | NPC decisions, pathfinding                 |
| `"audio"`    | Music changes, sound triggers              |
| `"network"`  | Multiplayer events, sync issues            |
| `"save"`     | Save/load operations                       |
| `"_engine"`  | Reserved — Logger bridge                   |
| `"_glog"`    | Reserved — synthetic events                |

## File Structure

```
addons/GLog/
├── plugin.cfg
├── plugin.gd        ← registers GLog autoload
├── GLog.gd          ← core: session lifecycle, event API, buffer, flush thread
├── glog_config.gd   ← all config vars with defaults
├── glog_bridge.gd   ← Godot Logger bridge for engine error capture
└── glog_llm.gd      ← LLM export logic
```
