extends Node
## Phase 3 Validation: 10-node compiled graph test.
## Expected: Two sine waves (440Hz + 880Hz) mixed together, heard through speakers.
## Validates: GraphCompiler, topological sort, arena allocation, pin binding.

@onready var player: AudioStreamPlayer = $AudioStreamPlayer


func _ready() -> void:
	var stream: AudioStreamSymphony = player.stream as AudioStreamSymphony
	stream.load_test_graph()

	print("Phase 3: 10-node graph compiled and loaded.")
	print("  Nodes: 3x Constant, 2x Oscillator, 3x Gain, 1x MathAdd, 1x ADSR, 1x GraphOutput")
	print("  Expected: 440Hz + 880Hz sine waves mixed")
	print("  Press Escape to quit.")

	player.play()


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed("ui_cancel"):
		player.stop()
		print("Stopped.")
		get_tree().quit()
