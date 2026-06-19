extends Node

@onready var player: AudioStreamPlayer = $AudioStreamPlayer


func _ready() -> void:
	print("Symphony Phase 1 Test: Playing 440Hz sine wave...")
	player.play()


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed("ui_cancel"):
		player.stop()
		print("Stopped.")
		get_tree().quit()
