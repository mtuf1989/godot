# UIActivatable Scene Templates

Concrete templates for each common UIActivatable scene type. Copy and adapt these patterns.

## Table of Contents

1. [Minimal UIActivatable (Starter)](#minimal-uiactivatable)
2. [Main Menu](#main-menu)
3. [Pause Menu](#pause-menu)
4. [Game HUD](#game-hud)
5. [Settings Menu](#settings-menu)
6. [Confirmation Dialog (Modal)](#confirmation-dialog)
7. [Loading Screen](#loading-screen)
8. [World Scene (SceneRouter)](#world-scene)

---

## Minimal UIActivatable

The smallest valid UIActivatable. Use as a starting point.

### .tscn

```
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://path/to/my_screen.gd" id="1_script"]

[node name="MyScreen" type="Control"]
layout_mode = 3
anchors_preset = 15
anchor_right = 1.0
anchor_bottom = 1.0
script = ExtResource("1_script")
```

Key points:
- Root is `Control` with `anchors_preset = 15` (FULL_RECT).
- `layout_mode = 3` means the root uses anchor-based layout.
- Script extends UIActivatable.

### .gd

```gdscript
extends UIActivatable


func activate(context: Dictionary) -> void:
    visible = true


func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false


func get_desired_focus_target() -> Control:
    return null


func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.MENU, true, false)


func on_back_requested() -> bool:
    return false  # Let system pop this scene
```

---

## Main Menu

Layer: MENU. InputMode: MENU.

### .tscn structure

```
MyMainMenu (Control, anchors_preset=15, script=my_main_menu.gd)
  Background (ColorRect, anchors_preset=15, color=dark)
  CenterContainer (anchors_preset=15)
    VBox (VBoxContainer, custom_minimum_size.x=300)
      TitleLabel (Label, unique_name_in_owner=true, h_align=CENTER)
      Spacer (Control, custom_minimum_size.y=40)
      StartButton (Button, unique_name_in_owner=true, text="New Game")
      SettingsButton (Button, unique_name_in_owner=true, text="Settings")
      QuitButton (Button, unique_name_in_owner=true, text="Quit")
      VersionLabel (Label, unique_name_in_owner=true, h_align=CENTER)
```

### .gd pattern

```gdscript
extends UIActivatable

@onready var title_label: Label = %TitleLabel
@onready var start_button: Button = %StartButton
@onready var settings_button: Button = %SettingsButton
@onready var quit_button: Button = %QuitButton

func _ready() -> void:
    if start_button:
        start_button.pressed.connect(_on_start_pressed)
    if settings_button:
        settings_button.pressed.connect(_on_settings_pressed)
    if quit_button:
        quit_button.pressed.connect(_on_quit_pressed)

func activate(context: Dictionary) -> void:
    visible = true
    # Read context: context.get("player_name", "Player"), etc.

func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false

func get_desired_focus_target() -> Control:
    return start_button

func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.MENU, true, false)

func on_back_requested() -> bool:
    call_deferred("_show_quit_confirmation")
    return true

func _on_start_pressed() -> void:
    call_deferred("_start_game")

func _on_settings_pressed() -> void:
    call_deferred("_open_settings")

func _on_quit_pressed() -> void:
    call_deferred("_show_quit_confirmation")

func _start_game() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if ui_router:
        ui_router.clear_layer_with_transition(UILayer.Layer.MENU)
        ui_router.push_to_layer(UILayer.Layer.GAME_HUD, "res://path/to/game_hud.tscn", {
            "player_name": "Player", "level": 1, "health": 100, "score": 0
        })

func _open_settings() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if ui_router:
        ui_router.push_to_layer_with_transition(
            UILayer.Layer.MENU,
            "res://path/to/settings_menu.tscn",
            {"return_to": "main_menu", "current_settings": {}},
            true, 0.25
        )

func _show_quit_confirmation() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if ui_router:
        ui_router.push_to_layer_with_transition(
            UILayer.Layer.MODAL,
            "res://path/to/confirmation_dialog.tscn",
            {"title": "Quit Game", "message": "Are you sure?",
             "confirm_action": "quit_game", "cancel_action": "dismiss"},
            true, 0.2
        )
```

---

## Pause Menu

Layer: GAME_MENU. InputMode: MENU.

### .gd pattern

```gdscript
extends UIActivatable

@onready var resume_button: Button = %ResumeButton
@onready var settings_button: Button = %SettingsButton
@onready var quit_button: Button = %QuitButton

var _game_context: Dictionary = {}

func _ready() -> void:
    if resume_button:
        resume_button.pressed.connect(_on_resume_pressed)
    if settings_button:
        settings_button.pressed.connect(_on_settings_pressed)
    if quit_button:
        quit_button.pressed.connect(_on_quit_pressed)

func activate(context: Dictionary) -> void:
    visible = true
    _game_context = context

func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false

func get_desired_focus_target() -> Control:
    return resume_button

func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.MENU, true, false)

func on_back_requested() -> bool:
    call_deferred("_resume_game")
    return true  # Back = resume

func _on_resume_pressed() -> void:
    call_deferred("_resume_game")

func _resume_game() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if ui_router:
        ui_router.pop_from_layer_with_transition(UILayer.Layer.GAME_MENU, true, 0.2)
```

Key: back navigation resumes the game (pops the pause menu).

---

## Game HUD

Layer: GAME_HUD. InputMode: GAME.

### .gd pattern

```gdscript
extends UIActivatable

@onready var health_bar: ProgressBar = %HealthBar
@onready var score_label: Label = %ScoreLabel

var _health: int = 100
var _score: int = 0

func activate(context: Dictionary) -> void:
    visible = true
    _health = context.get("health", 100) as int
    _score = context.get("score", 0) as int
    _update_display()

func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false

func get_desired_focus_target() -> Control:
    return null  # HUD must not steal focus from gameplay

func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.GAME, false, false)

func on_back_requested() -> bool:
    call_deferred("_open_pause_menu")
    return true  # Back = open pause

func _open_pause_menu() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if ui_router:
        ui_router.push_to_layer_with_transition(
            UILayer.Layer.GAME_MENU,
            "res://path/to/pause_menu.tscn",
            {"health": _health, "score": _score},
            true, 0.2
        )

func _update_display() -> void:
    if health_bar:
        health_bar.value = _health
    if score_label:
        score_label.text = "Score: " + str(_score)

# Public methods for game code to call
func update_health(value: int) -> void:
    _health = value
    _update_display()

func update_score(value: int) -> void:
    _score = value
    _update_display()
```

Key: GAME InputMode, null focus target, back opens pause on GAME_MENU layer.

---

## Settings Menu

Layer: MENU or GAME_MENU (context-dependent). InputMode: MENU.

### .gd pattern

```gdscript
extends UIActivatable

@onready var back_button: Button = %BackButton

func _ready() -> void:
    if back_button:
        back_button.pressed.connect(_on_back_pressed)

func activate(context: Dictionary) -> void:
    visible = true

func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false

func get_desired_focus_target() -> Control:
    return back_button

func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.MENU, true, false)

func on_back_requested() -> bool:
    call_deferred("_go_back")
    return true

func _on_back_pressed() -> void:
    call_deferred("_go_back")

func _go_back() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if ui_router:
        ui_router.pop_active_with_transition(true, 0.25)
```

Key: settings can live on MENU or GAME_MENU depending on where it was pushed from. Use `pop_active()` to pop without knowing which layer — no need to track the layer via context.

---

## Confirmation Dialog

Layer: MODAL. InputMode: LOCKED.

### .gd pattern

```gdscript
extends UIActivatable

@onready var title_label: Label = %TitleLabel
@onready var message_label: Label = %MessageLabel
@onready var confirm_button: Button = %ConfirmButton
@onready var cancel_button: Button = %CancelButton

var _dialog_context: Dictionary = {}

func _ready() -> void:
    if confirm_button:
        confirm_button.pressed.connect(_on_confirm_pressed)
    if cancel_button:
        cancel_button.pressed.connect(_on_cancel_pressed)

func activate(context: Dictionary) -> void:
    visible = true
    _dialog_context = context
    if context.has("title") and title_label:
        title_label.text = str(context["title"])
    if context.has("message") and message_label:
        message_label.text = str(context["message"])

func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false

func get_desired_focus_target() -> Control:
    return cancel_button  # Safer default

func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.LOCKED, true, false)

func on_back_requested() -> bool:
    call_deferred("_handle_cancel")
    return true  # Back = cancel

func _on_confirm_pressed() -> void:
    call_deferred("_handle_confirm")

func _on_cancel_pressed() -> void:
    call_deferred("_handle_cancel")

func _handle_confirm() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if not ui_router:
        return
    ui_router.pop_from_layer_with_transition(UILayer.Layer.MODAL, true, 0.15)
    var action: String = _dialog_context.get("confirm_action", "")
    match action:
        "quit_game":
            get_tree().quit()
        _:
            pass

func _handle_cancel() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if not ui_router:
        return
    ui_router.pop_from_layer_with_transition(UILayer.Layer.MODAL, true, 0.15)
```

Key: LOCKED InputMode, cancel_button gets default focus (safer), back = cancel.

### .tscn structure

```
ConfirmationDialog (Control, anchors_preset=15, script)
  Background (ColorRect, anchors_preset=15, color=Color(0,0,0,0.8))
  CenterContainer (anchors_preset=15)
    Panel (Panel, custom_minimum_size=Vector2(400,250))
      VBox (VBoxContainer, anchors_preset=15, margins=20)
        TitleLabel (Label, unique_name_in_owner=true, h_align=CENTER)
        Spacer (Control, custom_minimum_size.y=20)
        MessageLabel (Label, unique_name_in_owner=true, h_align=CENTER, autowrap=WORD_SMART)
        FlexSpacer (Control, size_flags_vertical=EXPAND)
        ButtonContainer (HBoxContainer)
          CancelButton (Button, unique_name_in_owner=true, size_flags_h=EXPAND, text="Cancel")
          Spacer (Control, custom_minimum_size.x=20)
          ConfirmButton (Button, unique_name_in_owner=true, size_flags_h=EXPAND, text="Confirm")
```

---

## Loading Screen

Layer: LOADING. InputMode: LOCKED.

### .gd pattern

```gdscript
extends UIActivatable

@onready var progress_bar: ProgressBar = %ProgressBar
@onready var message_label: Label = %MessageLabel

var _loading_context: Dictionary = {}

func activate(context: Dictionary) -> void:
    visible = true
    _loading_context = context
    if context.has("message") and message_label:
        message_label.text = str(context["message"])
    if progress_bar:
        progress_bar.value = 0.0
    if context.get("auto_load", false):
        call_deferred("_start_loading")

func deactivate(reason: DeactivateReason.Reason) -> void:
    visible = false

func get_desired_focus_target() -> Control:
    return null  # No focus during loading

func get_desired_input_config() -> UIInputConfig:
    return UIInputConfig.new(UIInputConfig.InputMode.LOCKED, false, false)

func on_back_requested() -> bool:
    return true  # Block back during loading

func update_progress(value: float) -> void:
    if progress_bar:
        progress_bar.value = clamp(value, 0.0, 1.0) * 100.0

func _start_loading() -> void:
    # Simulate or perform actual loading
    # When done, clear LOADING layer and push target scene
    pass

func _complete_loading() -> void:
    var ui_router: Node = get_node("/root/UIRouter")
    if not ui_router:
        return
    ui_router.clear_layer(UILayer.Layer.LOADING)
    var next_scene: String = _loading_context.get("next_scene", "")
    if next_scene:
        var next_context: Dictionary = _loading_context.get("next_context", {})
        # Convert next_layer string to enum
        var layer: UILayer.Layer = UILayer.Layer.MENU
        match _loading_context.get("next_layer", "MENU"):
            "GAME_HUD": layer = UILayer.Layer.GAME_HUD
            "GAME_MENU": layer = UILayer.Layer.GAME_MENU
            "MENU": layer = UILayer.Layer.MENU
        ui_router.push_to_layer_with_transition(layer, next_scene, next_context, true, 0.5)
```

Key: LOADING layer (highest priority), LOCKED InputMode, blocks back navigation, clears itself before pushing next scene.

---

## World Scene (SceneRouter)

World scenes are NOT UIActivatable. They implement `apply_context()` instead.

### .gd pattern

```gdscript
extends Node2D  # or Node3D

@onready var label: Label = $Label

func apply_context(context: Dictionary) -> void:
    # Receive context from SceneRouter.change_world()
    if context.has("player_level"):
        print("Player level: ", context["player_level"])
```

### Triggering world transitions from UI

```gdscript
func _start_level(level_path: String, context: Dictionary) -> void:
    var scene_router: Node = get_node_or_null("/root/SceneRouter")
    if scene_router:
        scene_router.change_world(level_path, context, {
            "fade_out_duration": 0.5,
            "fade_in_duration": 0.5,
        })
```

### Listening to transition signals

```gdscript
func _ready() -> void:
    var scene_router: Node = get_node_or_null("/root/SceneRouter")
    if scene_router:
        scene_router.world_transition_started.connect(_on_transition_started)
        scene_router.world_transition_finished.connect(_on_transition_finished)

func _on_transition_started(scene_path: String) -> void:
    pass

func _on_transition_finished(scene_path: String) -> void:
    pass
```
