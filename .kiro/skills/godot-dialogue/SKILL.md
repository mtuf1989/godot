---
name: godot-dialogue
description: |
  Implement dialogue systems in Godot 4 using the Dialogue Manager addon (nathanhoad/godot_dialogue_manager).
  Write .dialogue script files, build custom balloon scenes, wire DialogueManager API calls in GDScript,
  set up conditions/mutations against game state, and configure dialogue settings.
  Use when the user needs to create dialogue trees, branching conversations, NPC dialogue,
  cutscene text, response menus, typed dialogue labels, or any narrative/text system
  that uses the Dialogue Manager addon.
  Also use when someone mentions DialogueManager, DialogueLine, DialogueResponse, DialogueResource,
  DialogueLabel, DialogueResponsesMenu, .dialogue files, dialogue balloons, cues, dialogue conditions,
  dialogue mutations, "show_dialogue_balloon", "get_next_dialogue_line", or asks to write conversation
  scripts, branching dialogue, or NPC text.
  Even if the user just says "make this character talk" or "add a conversation" or "dialogue system"
  in a Godot project that has the Dialogue Manager addon installed, this skill applies.
  Do NOT use for general UI layout (use godot-ui-core), audio/sound design, or AI behavior trees
  (use godot-limboai). Do NOT use if the project does not have the Dialogue Manager addon.
---

# Godot Dialogue

Implement dialogue systems using the Dialogue Manager addon with correct `.dialogue` syntax, proper balloon wiring, and validated game state integration.

Read `references/dialogue-syntax-reference.md` before writing any `.dialogue` file. Read `references/balloon-and-integration-checklist.md` before building or modifying balloon scenes.

## Responsibility

- Write `.dialogue` script files with correct syntax (cues, jumps, conditions, mutations, responses, tags, imports, random lines, concurrent lines).
- Build or modify custom balloon scenes that display `DialogueLine` data using `DialogueLabel` and `DialogueResponsesMenu`.
- Wire `DialogueManager.show_dialogue_balloon()` or `get_next_dialogue_line()` calls in game scripts.
- Set up game state objects (autoloads, extra game states) so dialogue conditions and mutations resolve correctly.
- Configure Dialogue Manager project settings when needed (balloon path, state shortcuts, translation source).
- Set up `Actionable2D`/`Actionable3D` nodes for proximity-triggered dialogue.
- Implement dialogue save/load using `DialogueLine.to_serialized()` / `new_from_serialized()`.
- Create `DMDialogueProcessor` subclasses for compile-time text transformation when needed.
- Validate that `.dialogue` files compile without errors and that runtime wiring is correct.

## Use When

- The task requires writing or editing `.dialogue` script files.
- A balloon scene needs to be created, customized, or connected.
- Game scripts need to trigger, advance, or react to dialogue.
- Conditions or mutations in dialogue need to reference game state.
- Dialogue Manager settings need configuration.
- The user wants branching conversations, NPC dialogue, cutscene text, or response menus.

## Do Not Use When

- The project does not have the Dialogue Manager addon installed.
- The task is pure UI layout with no dialogue content (use `godot-ui-core`).
- The task is AI behavior/decision-making (use `godot-limboai`).
- Architecture decisions about whether to use Dialogue Manager at all are still open (escalate to `godot-architect`).
- The task is about save/load of dialogue progress (escalate to `godot-persistence`, then return here for implementation).

## Workflow

1. Inspect the project to confirm the Dialogue Manager addon is present (`addons/dialogue_manager/plugin.cfg`). Check that the `DialogueManager` autoload is registered. If missing, report the blocker. In editor-connected mode, use `find_nodes` to locate `DialogueLabel` or `DialogueResponsesMenu` nodes in existing balloon scenes.

2. Read `references/dialogue-syntax-reference.md` to ground yourself in the exact syntax before writing any `.dialogue` content.

3. Identify what the task requires:
   - **New dialogue content**: Write `.dialogue` files with cues, character lines, responses, conditions, mutations, jumps.
   - **Balloon scene**: Create or modify a balloon scene with `DialogueLabel`, `DialogueResponsesMenu`, input handling, and the `DialogueManager` signal wiring.
   - **Game integration**: Wire `show_dialogue_balloon()` or `get_next_dialogue_line()` in the calling script. Set up `extra_game_states` if dialogue needs to read/write local state.
   - **Settings**: Configure balloon path, state autoload shortcuts, or translation source in project settings.

4. For `.dialogue` files:
   - Start every file with a `~ start` cue (or the cue name specified by the task).
   - Use `=> END` to terminate flows. Use `=> END!` only when forcing conversation termination across snippet chains.
   - Conditions referencing game state must use the exact autoload/variable names. Verify the target autoload or extra game state exists.
   - Mutations use `$>` (awaited) or `$>>` (fire-and-forget). Use `set` for simple assignments.
   - Response conditions use self-closing syntax: `[if condition /]`.
   - Use `[#tag]` for metadata. Use `[ID:KEY]` for translation-ready lines.
   - Use `import "res://path.dialogue" as alias` for cross-file references, and `=>< alias/cue` for jump-and-return.

5. For balloon scenes:
   - The balloon script receives a `DialogueLine` from `DialogueManager`.
   - Use `DialogueLabel` for typed text output. Call `type_out()` to start, listen for `finished_typing`.
   - Use `DialogueResponsesMenu` for response buttons. Listen for `response_selected`.
   - Advance dialogue with `await DialogueManager.get_next_dialogue_line(resource, next_id)`.
   - When `get_next_dialogue_line` returns `null`, the conversation is over. Call `queue_free()`.
   - Connect to `DialogueManager.dialogue_ended` if cleanup is needed.

6. For game integration scripts:
   - Load the dialogue resource: `var resource := load("res://path/to/file.dialogue") as DialogueResource`.
   - Show dialogue: `DialogueManager.show_dialogue_balloon(resource, "cue_name")` or `DialogueManager.show_dialogue_balloon_scene(balloon_scene, resource, "cue_name")`.
   - Pass extra game states as the third argument when dialogue needs access to local objects.
   - Use `using AutoloadName` in `.dialogue` files or configure State Autoload Shortcuts in settings to shorten state references.

7. Write files to disk using native file editing. After writing `.dialogue` or `.gd` files, call `refresh_filesystem` to sync the editor.

8. Validate:
   - In editor-connected mode: use `get_diagnostics` on changed `.gd` scripts. Use `run_scene` if a test scene exists to verify dialogue plays without runtime errors.
   - Check that `.dialogue` files have no compilation errors (the import plugin compiles on save; errors surface in the Errors Panel or console).
   - Verify that all state references in conditions/mutations point to real autoloads, properties, or methods.

9. If the task requires features beyond this skill's boundary (save/load of dialogue progress, complex UI layout, AI-driven dialogue selection), escalate to the appropriate skill.

## Output

- `.dialogue` files with correct syntax and cue structure.
- Balloon scene (`.tscn`) and script (`.gd`) if creating or modifying a balloon.
- Game integration script changes wiring `DialogueManager` calls.
- Settings changes if needed.
- Validation results and any remaining blockers.

## Quality Gates

- `.dialogue` files compile without errors.
- All cue references (`=>`) resolve to existing cues in the same file or imported files.
- Conditions and mutations reference real game state (autoloads, extra game states, or scene properties).
- Balloon scenes use `DialogueLabel` and `DialogueResponsesMenu` correctly.
- Response conditions use the v4 self-closing syntax `[if expr /]`, not the old `[if expr]`.
- No Godot 3 / Dialogue Manager v3 idioms (e.g., "titles" instead of "cues").
- `refresh_filesystem` is called after disk writes before editor validation.
- Typed GDScript throughout (variables, parameters, return types).

## Common Pitfalls

These are runtime traps discovered from source code analysis. Be aware of them when writing dialogue integration:

1. **`get_next_dialogue_line` MUST be awaited.** It's async. Forgetting `await` returns null and silently breaks the flow.

2. **The system is stateless.** You must pass `next_id` from the previous line to advance. If you lose `next_id`, you lose your place. Use `line.to_serialized()` / `DialogueLine.new_from_serialized()` for save/load.

3. **Autoloads are cached on first dialogue call.** `DialogueManager._get_game_states()` caches autoloads once. Autoloads added dynamically after the first dialogue call won't be found.

4. **`game_states` is overwritten on first use.** If you manually set `DialogueManager.game_states` before any dialogue runs, it gets overwritten with `[_autoloads]` on first call.

5. **Balloon `start()` is called deferred.** `_start_balloon` uses `call_deferred`. Don't interact with the balloon on the same frame you call `show_dialogue_balloon()`.

6. **Inline mutations fire synchronously on skip.** When `skip_typing()` is called, all remaining inline mutations fire at once without awaiting. Timing-dependent mutations (animations, sounds) will stack.

7. **`mutated` signal does NOT emit for assignments.** Only non-assignment mutations emit the `mutated` signal. Don't rely on it for tracking variable changes.

8. **`dialogue_ended` fires for ANY resource ending.** If multiple Actionable nodes share the same `DialogueResource`, the signal fires for all of them. Filter by resource in your handler.

9. **Missing state crashes in debug.** Referencing a non-existent property/method triggers `assert(false)` in debug builds. Set `ignore_missing_state_values = true` during development to downgrade to `push_error()`.

10. **Override `get_current_scene` for custom scene management.** The default uses `Engine.get_main_loop().current_scene`. If your game stacks scenes or uses a custom root, balloons will be added to the wrong parent.

11. **`[next=N]` is the BBCode, `DialogueLine.time` is the runtime property.** Don't confuse the dialogue syntax (`[next=2.5]`) with the runtime field name (`.time`).

12. **Random blocks with all-failing conditions skip entirely.** If every sibling in a `%` random block fails its condition, the block is skipped — no fallback to unconditional lines.

## Failure Modes

- Do not invent dialogue syntax that does not exist. The `.dialogue` format is specific. Refer to `references/dialogue-syntax-reference.md`.
- Do not assume the Dialogue Manager addon is installed without checking.
- Do not write balloon logic that skips the `DialogueLabel`/`DialogueResponsesMenu` pattern unless the task explicitly requires a fully custom renderer.
- Do not use `[if condition]` (non-self-closing) for response conditions. The v4 syntax is `[if condition /]`.
- Do not reference state objects that do not exist in the project.
- Do not silently add autoloads or project settings without stating what was changed.
- Do not claim validation that was not performed.
- Do not use `titles` (v3 term). Use `cues`.
- Do not use `[time=N]` in dialogue files. The correct BBCode is `[next=N]`.

## References

Read only as needed:

- `references/dialogue-syntax-reference.md` -- complete syntax with examples, built-in functions table, traps for random blocks/imports/inline mutations
- `references/balloon-and-integration-checklist.md` -- balloon scene setup, 8 integration patterns (including Actionable nodes, MutationBehaviour, serialization, runtime resource creation, DMDialogueProcessor)
- `../../foundation/Godot Nuanced Development Practices.md`
