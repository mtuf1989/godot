# Dialogue Script Syntax Reference

This is the complete syntax for `.dialogue` files used by the Dialogue Manager addon (Godot 4.6+, v4 syntax).

## File Structure

A `.dialogue` file is plain text with indentation-based nesting. No YAML, no JSON. Lines are parsed top-to-bottom.

Optional file-level directives go at the top:

```
import "res://snippets.dialogue" as snippets
using SomeAutoload
```

Then cues and dialogue content follow.

## Cues (Entry Points)

```
~ cue_name
```

Cues are named markers. The runtime starts traversal from a cue. The default convention is `~ start`. Cue names cannot contain spaces or begin with a number.

## Dialogue Lines

```
Character: This is what they say.
```

Plain text (no character) is also valid:

```
This is narration with no speaker.
```

BBCode is supported in text:

```
Nathan: This is [b]bold[/b] and [i]italic[/i].
```

### Special BBCodes (Dialogue Manager additions)

| BBCode | Effect |
|--------|--------|
| `[[Option A\|Option B\|Option C]]` | Pick one at random inline |
| `[wait=N]` | Pause typing for N seconds |
| `[wait="action_name"]` | Pause typing until input action |
| `[wait]` | Pause typing until any action |
| `[speed=N]` | Multiply typing speed by N (e.g., 0.5 = half speed, 2 = double) |
| `[next=N]` | Auto-advance to next line after N seconds (once typing finishes) |
| `[next=auto]` | Auto-advance based on text length (approx 0.02s per character) |

**Important:** `[next=N]` is the BBCode tag in `.dialogue` files. At runtime, the value appears as `DialogueLine.time` (a String — either a float string, `"auto"`, or `""` for no auto-advance).

## Responses

Responses start with `- ` and can nest dialogue below them:

```
Nathan: What do you think?
- I agree
	Nathan: Great!
- I disagree
	Nathan: That's too bad.
- Tell me more => more_info
```

Responses can have conditions (v4 self-closing syntax):

```
- Secret option [if GameState.has_key == true /]
	Nathan: You found it!
```

Responses can have inline jumps:

```
- Go away => END
- Tell me about the quest => quest_info
```

If combining a condition and a jump on a response, the jump goes last:

```
- Special option [if has_item /] => special_cue
```

## Jumps (Gotos)

```
=> cue_name          # Jump to cue
=> END               # End current flow
=> END!              # Force end entire conversation (even across snippets)
=>< cue_name         # Jump and return (snippet call)
```

### Expression Jumps

Dynamic goto targets resolved at runtime. The compiler cannot verify these — use with caution:

```
=> {{SomeGlobal.next_cue}}
```

This evaluates the expression and jumps to whatever cue name it returns.

## Conditions

### Block conditions

```
if SomeGlobal.some_property >= 10
	Nathan: High value!
elif SomeGlobal.some_property >= 5
	Nathan: Medium value.
else
	Nathan: Low value.
```

Conditions support `and`, `or`, `not`, and grouping with `()`:

```
if has_key and (door_unlocked or has_lockpick)
	Nathan: We can get through.
```

### Inline conditions

```
Nathan: I have [if already_visited]been here before[else]never been here[/if].
Nathan: You have {{count}} [if count == 1]apple[else]apples[/if].
```

### Response conditions (self-closing, v4 syntax)

```
- Option A [if condition /]
```

### Match blocks

```
match SomeGlobal.mood
	when "happy"
		Nathan: I'm glad!
	when "sad"
		Nathan: That's rough.
	else
		Nathan: Hmm.
```

Multiple values in a single `when`:

```
match SomeGlobal.mood
	when "happy", "excited"
		Nathan: Great vibes!
```

### While loops

```
while SomeGlobal.counter < 3
	Nathan: Counter is {{SomeGlobal.counter}}.
	$> SomeGlobal.counter += 1
```

## Mutations

Awaited mutation (dialogue pauses until complete):

```
$> SomeGlobal.do_something()
```

Fire-and-forget mutation (dialogue advances immediately):

```
$>> SomeGlobal.do_something()
```

Assignment shorthand:

```
set SomeGlobal.has_met = true
set SomeGlobal.counter += 1
```

### Inline mutations (execute as text types past that point)

```
Nathan: Let me show you [$> animate("wave")]something.
```

Inline fire-and-forget:

```
Nathan: Background effect [$>> play_sound("click")]here.
```

**Trap:** When the player skips typing (`skip_typing()`), all remaining inline mutations fire synchronously without awaiting. If your inline mutations depend on timing (animations, sounds), they'll all fire at once.

### Signal emission

Emit signals on any game state object:

```
$> SomeGlobal.some_signal.emit("argument")
$> SomeGlobal.quest_completed.emit()
```

### Null coalescing

Safe navigation for potentially null references:

```
if some_ref?.name == "Target"
	Nathan: Found it.
```

If `some_ref` is null, the entire expression resolves to null (no crash).

## Built-in Functions

These are available in any mutation or expression without needing a game state reference:

| Function | Purpose | Example |
|----------|---------|---------|
| `wait(seconds)` | Pause execution for N seconds | `$> wait(2.0)` |
| `wait("action")` | Pause until input action pressed | `$> wait("ui_accept")` |
| `wait(["a","b"])` | Pause until any of the listed actions | `$> wait(["jump","interact"])` |
| `debug(args...)` | Print to Output console | `$> debug("value:", score)` |
| `roll_dice(sides)` | Random integer from 1 to N | `if roll_dice(20) >= 15` |
| `load("path")` | Load a resource at runtime | `$> load("res://sfx.tres")` |
| `str(value)` | Convert any value to String | `{{str(count)}}` |
| `Vector2(x, y)` | Construct Vector2 | `set pos = Vector2(10, 20)` |
| `Vector2i(x, y)` | Construct Vector2i | |
| `Vector3(x, y, z)` | Construct Vector3 | |
| `Vector3i(x, y, z)` | Construct Vector3i | |
| `Vector4(x, y, z, w)` | Construct Vector4 | |
| `Vector4i(x, y, z, w)` | Construct Vector4i | |
| `Color(r, g, b, a)` | Construct Color (0-4 args) | `set tint = Color(1, 0, 0)` |
| `Quaternion(x, y, z, w)` | Construct Quaternion | |
| `Callable(obj, method)` | Construct Callable | |

### Built-in type methods

All methods on these types are callable in expressions:
- **String**: `length()`, `to_upper()`, `to_lower()`, `split()`, `contains()`, etc.
- **Array**: `size()`, `append()`, `has()`, `filter()`, `map()`, etc.
- **Dictionary**: `has()`, `keys()`, `values()`, `size()`, `get()`, `erase()`, etc.
- **Vector2/3/4**: `normalized()`, `length()`, `distance_to()`, etc.
- **Color**: `lerp()`, `lightened()`, `darkened()`, static `from_hsv()`, etc.
- **Quaternion**: `normalized()`, `slerp()`, static `from_euler()`, etc.

## Variable Interpolation

```
Nathan: Your score is {{GameState.score}}.
{{GameState.speaker_name}}: Dynamic character name.
```

Expressions inside `{{}}` can be any valid expression including method calls:

```
Nathan: You have {{inventory.size()}} items.
```

## Tags

```
Nathan: [#happy, #surprised] Oh hello!
Nathan: [#mood=excited] Let's go!
```

Access at runtime: `line.tags` returns `PackedStringArray`, `line.has_tag("mood")` returns `true`, `line.get_tag_value("mood")` returns `"excited"`.

Tags also work on responses:

```
- Accept the quest [#quest_accept]
- Decline [#quest_decline]
```

## Translation IDs

```
Nathan: Hello there! [ID:GREETING_01]
```

Lines with `[ID:KEY]` use the key as the translation context for `tr()`.

## Random Lines

Equal weight:

```
% Nathan: Maybe this.
% Nathan: Or this.
% Nathan: Or even this.
```

Weighted (number after `%` is relative weight):

```
%3 Nathan: 60% chance (weight 3 out of 5 total).
%2 Nathan: 40% chance (weight 2 out of 5 total).
```

Random blocks (multi-line random groups):

```
%
	Nathan: Block one, line one.
	Nathan: Block one, line two.
% Nathan: Or just this single line.
```

Separate groups with blank lines:

```
% Group A line 1
% Group A line 2

% Group B line 1
% Group B line 2
```

Random lines can have conditions:

```
% [if SomeGlobal.flag] => conditional_cue
%2 => default_cue
```

**Trap:** If ALL siblings in a random block fail their conditions, the system skips the entire block and advances to the next line. It does NOT fall back to an unconditional line.

## Concurrent Lines

```
Nathan: I'll say this.
| Coco: And I'll say this at the same time!
| Lilly: Me too!
```

Access via `line.concurrent_lines` array on the `DialogueLine`. The example balloon does NOT support concurrent lines — you must implement display yourself.

## Imports

```
import "res://common_snippets.dialogue" as snippets

~ start
Nathan: Let me tell you something.
=>< snippets/banter
Nathan: That was fun.
=> END
```

**Trap:** Circular imports are not supported. If file A imports file B, file B must compile without errors first. Import order matters.

## State Shortcuts

File-level:

```
using SomeAutoload
```

Or configure globally in Project Settings > Dialogue Manager > Runtime > State Autoload Shortcuts.

With shortcuts, `SomeAutoload.property` becomes just `property` in dialogue expressions.

**Trap:** If a `using` references a non-existent autoload, it logs an error but doesn't crash. Conditions/mutations referencing that state will then silently fail.

## Comments

```
# This is a comment (not compiled)
## This is a translator note (appears in POT export for translators)
```

## Escaping Keywords

To start a dialogue line with a keyword like "if", "set", "while", prefix with `\`:

```
\if you want to start a line with "if", escape it.
\set your mind at ease.
```

## Local Variables

Local variables are a pattern provided by the example balloon, not a built-in feature. The balloon passes a dictionary as an extra game state:

```
set locals.visited = true
if locals.visited
	Nathan: Welcome back.
```

Variables prefixed with `locals.` read/write to the balloon's dictionary. They are cleared when the conversation ends (when the balloon is freed).
