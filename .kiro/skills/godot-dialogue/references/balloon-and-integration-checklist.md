# Balloon and Integration Checklist

## Confirming the Addon is Present

Before any dialogue work, verify:

1. `addons/dialogue_manager/plugin.cfg` exists.
2. `DialogueManager` is registered as an autoload in `project.godot` (under `[autoload]`).
3. The plugin is enabled in `project.godot` under `[editor_plugins]`.

If any of these are missing, report the blocker. Do not attempt to manually register the autoload or enable the plugin through file edits unless the task explicitly asks for addon installation.

## Balloon Scene Structure

A balloon is a scene that receives dialogue lines and renders them. The standard pattern:

### Scene tree

```
Balloon (CanvasLayer or Control)
  Panel (PanelContainer or similar)
    MarginContainer
      VBoxContainer
        CharacterLabel (Label or RichTextLabel)
        DialogueLabel (DialogueLabel node)
        ResponsesMenu (DialogueResponsesMenu node)
```

The exact layout varies per game. The critical nodes are:

- `DialogueLabel` -- types out `line.text` with BBCode, speed, wait, and inline mutation support.
- `DialogueResponsesMenu` -- creates response buttons from `line.responses`.

### Balloon script pattern (GDScript)

```gdscript
extends CanvasLayer  # or Control, depending on your setup

@onready var character_label: Label = %CharacterLabel
@onready var dialogue_label: DialogueLabel = %DialogueLabel
@onready var responses_menu: DialogueResponsesMenu = %ResponsesMenu

var resource: DialogueResource
var temporary_game_states: Array = []
var dialogue_line: DialogueLine
var is_waiting_for_input: bool = false

func start(with_resource: DialogueResource, cue: String = "", extra_game_states: Array = []) -> void:
	temporary_game_states = [self] + extra_game_states
	resource = with_resource
	dialogue_line = await resource.get_next_dialogue_line(cue, temporary_game_states)
	_show_line(dialogue_line)


func _ready() -> void:
	responses_menu.response_selected.connect(_on_response_selected)
	dialogue_label.finished_typing.connect(_on_dialogue_label_finished_typing)


func _show_line(line: DialogueLine) -> void:
	if line == null:
		queue_free()
		return

	dialogue_line = line
	character_label.text = line.character
	dialogue_label.dialogue_line = line
	dialogue_label.type_out()

	responses_menu.hide()
	is_waiting_for_input = false

	# Handle auto-advance via [next=N] or [next=auto]
	if line.time != "":
		await dialogue_label.finished_typing
		var time: float = line.text.length() * 0.02 if line.time == "auto" else line.time.to_float()
		await get_tree().create_timer(time).timeout
		_advance(line.next_id)


func _on_dialogue_label_finished_typing() -> void:
	if dialogue_line.responses.size() > 0:
		responses_menu.responses = dialogue_line.responses
		responses_menu.show()
	else:
		is_waiting_for_input = true


func _on_response_selected(response: DialogueResponse) -> void:
	_advance(response.next_id)


func _advance(next_id: String) -> void:
	var next_line := await resource.get_next_dialogue_line(next_id, temporary_game_states)
	_show_line(next_line)


func _unhandled_input(event: InputEvent) -> void:
	# Skip typing if still typing
	if dialogue_label.is_typing:
		if event.is_action_pressed(&"ui_cancel"):
			get_viewport().set_input_as_handled()
			dialogue_label.skip_typing()
		return

	if not is_waiting_for_input:
		return
	if dialogue_line.responses.size() > 0:
		return

	if event.is_action_pressed(&"ui_accept"):
		get_viewport().set_input_as_handled()
		is_waiting_for_input = false
		_advance(dialogue_line.next_id)
```

This is a minimal skeleton. Real balloons add:
- Audio playback per character/line (using tags like `[#voice=res://audio/hello.ogg]`).
- Animation (fade in/out, slide).
- Portrait display based on tags like `[#mood=happy]`.
- Hide balloon during long mutations (the example balloon does this with a cooldown timer).

### Auto-start pattern (balloon as a scene node)

The example balloon supports being placed directly in a scene with exported properties:

```gdscript
@export var dialogue_resource: DialogueResource
@export var start_from_cue: String = ""
@export var auto_start: bool = false

func _ready() -> void:
	if auto_start:
		start()
```

This lets designers place dialogue triggers in the scene tree without code.

### DialogueLabel properties

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `seconds_per_step` | float | 0.02 | Base typing speed (seconds per character) |
| `pause_at_characters` | String | ".?!" | Characters that trigger a brief pause |
| `skip_pause_at_character_if_followed_by` | String | ")\"" | Don't pause if next char is one of these |
| `skip_pause_at_abbreviations` | PackedStringArray | ["Mr","Mrs","Ms","Dr","etc","eg","ex"] | Don't pause after these abbreviations |
| `seconds_per_pause_step` | float | 0.3 | Duration of auto-pause |
| `skip_action` | StringName | "ui_cancel" | Action to skip typing |

Key signals:

| Signal | Parameters | Purpose |
|--------|-----------|---------|
| `started_typing` | — | Typing began |
| `finished_typing` | — | All text visible |
| `skipped_typing` | — | Player skipped |
| `spoke` | `letter: String, letter_index: int, speed: float` | Each character typed (for lip sync, SFX) |

Call `type_out()` to start. Call `skip_typing()` to jump to end.

### DialogueResponsesMenu properties

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `response_template` | Control | null | Custom control to duplicate for each response |
| `next_action` | StringName | "" | Action for selecting (overridden by balloon) |
| `auto_configure_focus` | bool | true | Auto-setup keyboard navigation |
| `auto_focus_first_item` | bool | true | Focus first response on show |
| `hide_failed_responses` | bool | false | Hide responses where `is_allowed == false` |

The menu duplicates `response_template` for each response. If no template is set, it creates `Button` nodes. Each duplicated item gets a `"response"` meta with the `DialogueResponse` object.

Key signals: `response_selected(response: DialogueResponse)`, `response_focused(response: DialogueResponse)`.

## Game Integration Patterns

### Pattern 1: show_dialogue_balloon (simplest)

```gdscript
func _on_interact() -> void:
	var resource := load("res://dialogue/npc_greeting.dialogue") as DialogueResource
	DialogueManager.show_dialogue_balloon(resource, "start")
```

Uses the balloon configured in Project Settings > Dialogue Manager > Runtime > Balloon Path.

### Pattern 2: show_dialogue_balloon_scene (multiple balloon styles)

```gdscript
var cutscene_balloon: PackedScene = preload("res://ui/cutscene_balloon.tscn")

func _on_cutscene_trigger() -> void:
	var resource := load("res://dialogue/intro.dialogue") as DialogueResource
	DialogueManager.show_dialogue_balloon_scene(cutscene_balloon, resource, "intro")
```

### Pattern 3: Manual get_next_dialogue_line (full control)

```gdscript
var resource := load("res://dialogue/puzzle.dialogue") as DialogueResource
var line := await DialogueManager.get_next_dialogue_line(resource, "start", [self])

while line != null:
	_display_line(line)
	await _wait_for_player_input()
	if line.responses.size() > 0:
		var chosen: DialogueResponse = await _show_choices(line.responses)
		line = await DialogueManager.get_next_dialogue_line(resource, chosen.next_id, [self])
	else:
		line = await DialogueManager.get_next_dialogue_line(resource, line.next_id, [self])
```

#### MutationBehaviour parameter

The fourth argument to `get_next_dialogue_line` controls how mutations are handled:

```gdscript
# Wait for mutations to complete before returning the next line (default)
var line = await DialogueManager.get_next_dialogue_line(resource, key, [], DMConstants.MutationBehaviour.Wait)

# Fire mutations but advance immediately without waiting
var line = await DialogueManager.get_next_dialogue_line(resource, key, [], DMConstants.MutationBehaviour.DoNotWait)

# Skip mutations entirely (useful for previewing dialogue or rewinding)
var line = await DialogueManager.get_next_dialogue_line(resource, key, [], DMConstants.MutationBehaviour.Skip)
```

### Pattern 4: Extra game states

```gdscript
func start_shop_dialogue() -> void:
	var shop_state := { "gold": player.gold, "has_discount": player.reputation > 50 }
	var resource := load("res://dialogue/shopkeeper.dialogue") as DialogueResource
	DialogueManager.show_dialogue_balloon(resource, "start", [shop_state])
```

In the `.dialogue` file, reference directly:

```
if gold >= 100
	Shopkeeper: You can afford this.
```

For object-based extra states, pass the object itself:

```gdscript
DialogueManager.show_dialogue_balloon(resource, "start", [self])
```

Then in dialogue, call methods on it:

```
$> open_door()
if is_door_open
	Nathan: After you.
```

### Pattern 5: Listening to DialogueManager signals

```gdscript
func _ready() -> void:
	DialogueManager.dialogue_started.connect(_on_dialogue_started)
	DialogueManager.dialogue_ended.connect(_on_dialogue_ended)
	DialogueManager.passed_cue.connect(_on_passed_cue)
	DialogueManager.mutated.connect(_on_mutated)

func _on_dialogue_started(resource: DialogueResource) -> void:
	# Pause gameplay, show UI overlay, etc.
	pass

func _on_dialogue_ended(resource: DialogueResource) -> void:
	# Resume gameplay
	pass

func _on_passed_cue(cue: String) -> void:
	# React to dialogue passing through a specific cue
	# Useful for triggering game events at specific conversation points
	pass
```

### Pattern 6: Actionable nodes (proximity-triggered dialogue)

`Actionable2D` and `Actionable3D` are helper nodes that bundle a `DialogueResource` + cue + trigger logic:

```gdscript
# Configure in inspector: dialogue_resource, dialogue_cue
# Then call action() when the player interacts:
func _on_player_interact() -> void:
	var actionable := Actionable2D.get_nearest_actionable_to(player.global_position)
	if actionable:
		actionable.action()
```

Key features:
- `@export var dialogue_resource: DialogueResource` — the resource to use
- `@export var dialogue_cue: String` — the cue to start from
- `action()` — starts dialogue using the configured balloon
- `Actionable2D.get_nearest_actionable_to(position)` — static method to find closest
- Signals: `actioned`, `dialogue_ended`
- All Actionable nodes auto-join the `"dialogue_actionables"` group
- The static `start_dialogue` callable can be overridden for custom balloon logic

### Pattern 7: Runtime dialogue creation

Create dialogue resources on the fly from text:

```gdscript
var text := "~ start\nNPC: Hello, traveler!\n- Who are you?\n\tNPC: I'm a merchant.\n- Goodbye => END"
var resource := DialogueManager.create_resource_from_text(text)
DialogueManager.show_dialogue_balloon(resource)
```

Useful for procedural dialogue, modding support, or testing.

### Pattern 8: Serialization (save/load dialogue state)

The system is stateless — it doesn't track position. For save/load, use the built-in serialization:

```gdscript
# Save current position
var save_data: String = current_line.to_serialized()
# Produces something like "uid_hash@line_id=>next_id"

# Restore later
var restored_line: DialogueLine = await DialogueLine.new_from_serialized(save_data, [self])
```

This re-fetches the line from the resource, so variable interpolation reflects current game state at load time.

## Overriding get_current_scene

By default, `DialogueManager` adds balloons to `Engine.get_main_loop().current_scene`. If your game uses custom scene management (multiple scenes loaded, scene stacking, etc.), override this:

```gdscript
func _ready() -> void:
	DialogueManager.get_current_scene = func() -> Node:
		return my_custom_scene_root
```

This affects where `show_dialogue_balloon()` adds the balloon node.

## Custom Dialogue Processor (DMDialogueProcessor)

For compile-time text transformation, create a script extending `DMDialogueProcessor`:

```gdscript
class_name MyDialogueProcessor extends DMDialogueProcessor

func _preprocess_line(raw_line: String) -> String:
	# Modify raw text before compilation
	# Example: auto-replace placeholders
	return raw_line.replace("{PLAYER}", "{{PlayerState.name}}")

func _process_line(line: DMCompiledLine) -> void:
	# Modify compiled line data after compilation
	pass
```

Set the path in Project Settings > Dialogue Manager > Editor > Advanced > Dialogue Processor Path.

## Settings Configuration

Settings live in `project.godot` under `[dialogue_manager]`. Key settings:

| Setting | Path | Purpose |
|---------|------|---------|
| Balloon path | `runtime/balloon_path` | Default balloon scene for `show_dialogue_balloon()` |
| State shortcuts | `runtime/state_autoload_shortcuts` | Autoload names accessible without `using` |
| Name conflict warnings | `runtime/warn_about_method_property_or_signal_name_conflicts` | Warn when states share names |
| Ignore missing state | `runtime/advanced/ignore_missing_state_values` | Downgrade missing-state crashes to errors |
| Uses .NET | `runtime/advanced/uses_dotnet` | Enable C# bridge |
| Wrap long lines | `editor/wrap_long_lines` | Editor display preference |
| Missing translations are errors | `editor/translations/missing_translations_are_errors` | Enforce `[ID:KEY]` on all lines |
| Dialogue processor | `editor/advanced/dialogue_processor_path` | Custom `DMDialogueProcessor` script |
| Custom test scene | `editor/advanced/custom_test_scene_path` | Override the F5 test scene |

## Validation Checklist

After writing dialogue and integration code:

1. All `.dialogue` files compile without errors (check console or Errors Panel).
2. All `=> cue_name` jumps resolve to existing cues in the same file or imported files.
3. All conditions/mutations reference existing autoloads, properties, or methods.
4. The balloon scene has `DialogueLabel` and `DialogueResponsesMenu` nodes (or the task explicitly uses a custom renderer).
5. The balloon script has a `start(resource, cue, extra_game_states)` method.
6. The balloon calls `queue_free()` (or equivalent cleanup) when `get_next_dialogue_line` returns `null`.
7. `refresh_filesystem` was called after disk writes.
8. `get_diagnostics` was run on changed `.gd` files.
9. If a test scene exists, `run_scene` confirms no runtime errors.
